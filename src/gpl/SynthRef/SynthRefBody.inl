// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
// Part of src/main.cpp, included there inside #if INFINITE_GPL: the Synth REF
// node body. COMPACT is a normal-width card (instrument, firmware, project,
// MIDI activity, meter, volume, a small live screen); FULL is the instrument:
// the firmware's screen, its buttons with their LEDs, encoders, faders,
// wheels, pads and keyboard, all sending the firmware's own control events,
// then presets, files, the MIDI controller and MIDI DIAGNOSTICS. Switching
// views never touches the running firmware. The layout is Synth REF's own,
// drawn from the instrument's functional organisation (no maker artwork).

   struct SrUiState
   {
      float encAngle[8] = {};
      float dragAcc[8] = {};
      int keyHeld = -1;
      float peak[2] = {};
      bool prevButtons[32] = {};
      bool prevPads[16] = {};
      int faderSent[6] = { -1, -1, -1, -1, -1, -1 };
      std::vector<Platform::MidiDeviceInfo> devices; // refreshed once a second, not per frame
      double devicesAt = -10.0;
      std::string faderCore; // core the faders were initialised for
   };

   SrUiState& SrUi(SynthRefNode* n)
   {
      if (!n->uiState)
         n->uiState = std::make_shared<SrUiState>();
      return *static_cast<SrUiState*>(n->uiState.get());
   }

   void SrDeleteTexture(unsigned int t)
   {
      GLuint id = t;
      glDeleteTextures(1, &id);
   }

   // Uploads the firmware screen when it changed. RGB565 as the firmware
   // draws it, sampled linearly so a scaled screen stays readable.
   GLuint SrLcdTexture(SynthRefNode* n)
   {
      const SynthRefCoreInfo* info = n->Info();
      uint32_t rev = 0;
      const uint16_t* px = n->LcdPixels(rev);
      if (info == nullptr || px == nullptr)
         return 0;
      GLint prev = 0;
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
      if (n->lcdTexture == 0)
      {
         SynthRefNode::sDeleteTexture = SrDeleteTexture;
         GLuint t = 0;
         glGenTextures(1, &t);
         glBindTexture(GL_TEXTURE_2D, t);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)info->lcdWidth, (GLsizei)info->lcdHeight, 0, GL_RGB,
                      GL_UNSIGNED_SHORT_5_6_5, nullptr);
         n->lcdTexture = t;
         n->lcdTextureRevision = rev + 1;
      }
      if (rev != n->lcdTextureRevision)
      {
         glBindTexture(GL_TEXTURE_2D, n->lcdTexture);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)info->lcdWidth, (GLsizei)info->lcdHeight, GL_RGB,
                         GL_UNSIGNED_SHORT_5_6_5, px);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
         n->lcdTextureRevision = rev;
      }
      glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
      return n->lcdTexture;
   }

   ImU32 SrAccent(int alpha = 255)
   {
      return IM_COL32((int)(gAudioTint.r * 255.0f), (int)(gAudioTint.g * 255.0f), (int)(gAudioTint.b * 255.0f), alpha);
   }

   void SrLabelCentered(ImDrawList* dl, ImVec2 c, ImU32 col, const char* text, float scale = 1.0f)
   {
      ImFont* font = ImGui::GetFont();
      const float size = ImGui::GetFontSize() * scale;
      const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
      dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, text);
   }

   void SrDrawScreen(SynthRefNode* n, ImVec2 tl, float size)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(tl.x + size, tl.y + size);
      dl->AddRectFilled(ImVec2(tl.x - 4, tl.y - 4), ImVec2(br.x + 4, br.y + 4), IM_COL32(12, 12, 14, 255), 6.0f);
      const GLuint tex = SrLcdTexture(n);
      if (tex != 0)
         dl->AddImage((ImTextureID)(intptr_t)tex, tl, br);
      else
      {
         dl->AddRectFilled(tl, br, IM_COL32(20, 22, 26, 255));
         SrLabelCentered(dl, ImVec2(tl.x + size * 0.5f, tl.y + size * 0.5f), IM_COL32(150, 150, 160, 255),
                         n->CoreReady() ? "no screen" : "no firmware");
      }
      ImGui::SetCursorScreenPos(tl);
      ImGui::Dummy(ImVec2(size, size));
   }

   // A momentary hardware button: sends press / release edges. Lit = its LED.
   void SrButton(SynthRefNode* n, SrUiState& ui, int id, const char* name, ImVec2 tl, ImVec2 size, bool led)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImGui::SetCursorScreenPos(tl);
      ImGui::PushID(1000 + id);
      ImGui::InvisibleButton("##srbtn", size);
      const bool held = ImGui::IsItemActive();
      ImGui::PopID();
      if (held != ui.prevButtons[id])
      {
         n->SendControl(SR_CTL_BUTTON, id, held ? 1 : 0);
         ui.prevButtons[id] = held;
      }
      if (held)
         n->uiButtons |= 1u << id;
      else
         n->uiButtons &= ~(1u << id);
      const bool touched = n->TouchAgeMs(kSrButton, id) < 200;
      const ImVec2 br(tl.x + size.x, tl.y + size.y);
      dl->AddRectFilled(tl, br, held || touched ? IM_COL32(95, 98, 108, 255) : IM_COL32(52, 54, 60, 255), 4.0f);
      dl->AddRect(tl, br, IM_COL32(20, 20, 24, 255), 4.0f);
      dl->AddRectFilled(ImVec2(tl.x + 5, tl.y + 4), ImVec2(tl.x + 13, tl.y + 7),
                        led ? IM_COL32(255, 70, 50, 255) : IM_COL32(70, 30, 28, 255), 1.0f);
      SrLabelCentered(dl, ImVec2(tl.x + size.x * 0.5f, tl.y + size.y * 0.6f), IM_COL32(225, 225, 230, 255), name, 0.78f);
      if (ImGui::IsItemHovered())
         SetAudioReadout(name, led ? "LED on" : "button");
   }

   // An endless encoder: drag up / down sends detents (one per 4 px).
   void SrEncoder(SynthRefNode* n, SrUiState& ui, int id, const char* name, ImVec2 c, float r)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
      ImGui::PushID(2000 + id);
      ImGui::InvisibleButton("##srenc", ImVec2(r * 2, r * 2));
      const bool active = ImGui::IsItemActive();
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();
      if (active)
      {
         ui.dragAcc[id] -= ImGui::GetIO().MouseDelta.y;
         const int detents = (int)(ui.dragAcc[id] / 4.0f);
         if (detents != 0)
         {
            n->SendControl(SR_CTL_ENCODER, id, std::clamp(detents, -127, 127));
            ui.dragAcc[id] -= detents * 4.0f;
            ui.encAngle[id] += detents * 0.26f;
         }
      }
      else
         ui.dragAcc[id] = 0.0f;
      // The physical knob turns the virtual one by exactly the detents the
      // firmware received, and its ring lights while it moves.
      const bool touched = n->TouchAgeMs(kSrEncoder, id) < 250;
      dl->AddCircleFilled(c, r, IM_COL32(36, 37, 42, 255), 32);
      dl->AddCircle(c, r, touched || active ? SrAccent() : IM_COL32(90, 92, 100, 255), 32, 2.0f);
      const float a = ui.encAngle[id] + (id < 8 ? n->encoderTurns[id] * 0.26f : 0.0f) - 1.5708f;
      dl->AddLine(ImVec2(c.x + std::cos(a) * r * 0.3f, c.y + std::sin(a) * r * 0.3f),
                  ImVec2(c.x + std::cos(a) * r * 0.85f, c.y + std::sin(a) * r * 0.85f), IM_COL32(235, 235, 240, 255), 2.5f);
      SrLabelCentered(dl, ImVec2(c.x, c.y + r + 9), IM_COL32(200, 200, 210, 255), name, 0.8f);
      if (hovered)
         SetAudioReadout(name, "endless encoder: drag up / down");
   }

   // A vertical fader 0..127 (wheel = springs back to `rest` on release).
   void SrFader(SynthRefNode* n, SrUiState& ui, int id, const char* name, ImVec2 tl, ImVec2 size, int rest, bool spring,
                bool pickupWait)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImGui::SetCursorScreenPos(tl);
      ImGui::PushID(3000 + id);
      ImGui::InvisibleButton("##srfader", size);
      const bool active = ImGui::IsItemActive();
      const bool hovered = ImGui::IsItemHovered();
      const bool released = ImGui::IsItemDeactivated();
      ImGui::PopID();
      int& v = n->uiFader[id];
      if (active)
      {
         const float t = 1.0f - (ImGui::GetIO().MousePos.y - tl.y - 6.0f) / (size.y - 12.0f);
         v = std::clamp((int)std::lround(t * 127.0f), 0, 127);
      }
      else if (released && spring)
         v = rest;
      if ((active || (released && spring)) && v != ui.faderSent[id])
      {
         n->SendControl(SR_CTL_FADER, id, v);
         ui.faderSent[id] = v;
      }
      const bool touched = n->TouchAgeMs(kSrFader, id) < 250;
      const float cx = tl.x + size.x * 0.5f;
      dl->AddRectFilled(ImVec2(cx - 2, tl.y + 4), ImVec2(cx + 2, tl.y + size.y - 4), IM_COL32(15, 15, 18, 255), 2.0f);
      const float y = tl.y + 6.0f + (size.y - 12.0f) * (1.0f - v / 127.0f);
      const ImU32 cap = pickupWait ? IM_COL32(240, 170, 40, 255) : (active || touched ? SrAccent() : IM_COL32(200, 200, 208, 255));
      dl->AddRectFilled(ImVec2(tl.x + 2, y - 6), ImVec2(tl.x + size.x - 2, y + 6), cap, 3.0f);
      SrLabelCentered(dl, ImVec2(cx, tl.y + size.y + 8), IM_COL32(200, 200, 210, 255), name, 0.8f);
      if (hovered)
      {
         char val[48];
         snprintf(val, sizeof(val), "%d%s", v, pickupWait ? " (waiting for pickup)" : "");
         SetAudioReadout(name, val);
      }
   }

   // `padLeds` >= 0: pad p lights with LED padLeds + p (SLOOP's pad lights).
   void SrPads(SynthRefNode* n, SrUiState& ui, ImVec2 tl, float w, int padLeds)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float gap = 6.0f, padW = (w - gap * 7.0f) / 8.0f, padH = 40.0f;
      for (int p = 0; p < 16; ++p)
      {
         const int row = p / 8, col = p % 8;
         const ImVec2 a(tl.x + col * (padW + gap), tl.y + row * (padH + gap));
         const ImVec2 b(a.x + padW, a.y + padH);
         ImGui::SetCursorScreenPos(a);
         ImGui::PushID(4000 + p);
         ImGui::InvisibleButton("##srpad", ImVec2(padW, padH));
         const bool held = ImGui::IsItemActive();
         const bool hovered = ImGui::IsItemHovered();
         ImGui::PopID();
         if (held != ui.prevPads[p])
         {
            n->SendControl(SR_CTL_PAD, p, held ? 100 : 0);
            ui.prevPads[p] = held;
         }
         n->uiPads = held ? (n->uiPads | (1u << p)) : (n->uiPads & ~(1u << p));
         const bool lit = held || ((n->midiPads >> p) & 1u) || (padLeds >= 0 && n->Led(padLeds + p) != 0);
         dl->AddRectFilled(a, b, lit ? SrAccent() : IM_COL32(44, 46, 52, 255), 5.0f);
         dl->AddRect(a, b, IM_COL32(20, 20, 24, 255), 5.0f);
         const std::string label = n->PadLabel(p);
         char num[8];
         snprintf(num, sizeof(num), "%d", p + 1);
         dl->AddText(ImVec2(a.x + 4, a.y + 2), IM_COL32(150, 150, 160, 255), num);
         SrLabelCentered(dl, ImVec2((a.x + b.x) * 0.5f, a.y + padH * 0.62f), lit ? IM_COL32(20, 20, 24, 255) : IM_COL32(225, 225, 230, 255),
                         label.empty() ? "" : label.c_str(), 0.85f);
         if (hovered)
            SetAudioReadout("pad", label.empty() ? num : label.c_str());
      }
      ImGui::SetCursorScreenPos(ImVec2(tl.x, tl.y + 2 * padH + gap));
      ImGui::Dummy(ImVec2(w, 2));
   }

   // The instrument's keys (SMK-37: 37 from C3, FM-1: 27 from F3). Key k is the
   // firmware's key k; the octave is the firmware's own (OCT -/+) when it
   // reports it, and C keys show their note. `keyLeds` >= 0: key k lights with
   // LED keyLeds + k (the FM-1's keys have LEDs).
   void SrKeyboard(SynthRefNode* n, SrUiState& ui, ImVec2 tl, float w, float h, int keyCount, int baseNote, int keyLeds)
   {
      static const bool blackKey[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
      keyCount = std::clamp(keyCount, 1, 64);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      int octave = n->Query(SR_Q_KEY_OCTAVE);
      if (octave == SR_QUERY_NONE)
         octave = 0;
      // White index of each key, and how many whites there are.
      int whiteIndex[64];
      int whites = 0;
      for (int k = 0; k < keyCount; ++k)
      {
         whiteIndex[k] = whites;
         if (!blackKey[(baseNote + k) % 12])
            ++whites;
      }
      const float whiteW = w / (float)std::max(1, whites), blackW = whiteW * 0.62f, blackH = h * 0.6f;
      auto keyRect = [&](int k, ImVec2& a, ImVec2& b) {
         if (!blackKey[(baseNote + k) % 12])
         {
            const float x = tl.x + whiteIndex[k] * whiteW;
            a = ImVec2(x, tl.y);
            b = ImVec2(x + whiteW, tl.y + h);
            return false;
         }
         const float x = tl.x + whiteIndex[k] * whiteW - blackW * 0.5f; // between the previous white and the next
         a = ImVec2(x, tl.y);
         b = ImVec2(x + blackW, tl.y + blackH);
         return true;
      };
      ImGui::SetCursorScreenPos(tl);
      ImGui::InvisibleButton("##srkeys", ImVec2(w, h));
      const bool active = ImGui::IsItemActive();
      int hit = -1;
      float velT = 0.5f;
      if (active || ImGui::IsItemHovered())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         for (int pass = 0; pass < 2 && hit < 0; ++pass)
            for (int k = 0; k < keyCount; ++k)
            {
               ImVec2 a, b;
               const bool black = keyRect(k, a, b);
               if (black == (pass == 0) && m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y)
               {
                  hit = k;
                  velT = (m.y - a.y) / (b.y - a.y);
                  break;
               }
            }
      }
      const int want = active ? hit : -1;
      if (want != ui.keyHeld)
      {
         if (ui.keyHeld >= 0)
            n->SendControl(SR_CTL_KEY, ui.keyHeld, 0);
         if (want >= 0)
            n->SendControl(SR_CTL_KEY, want, std::clamp((int)(30 + velT * 97.0f), 1, 127)); // lower on the key = harder
         ui.keyHeld = want;
      }
      n->uiKeys = want >= 0 ? (1ull << want) : 0ull;
      for (int pass = 0; pass < 2; ++pass)
         for (int k = 0; k < keyCount; ++k)
         {
            ImVec2 a, b;
            const bool black = keyRect(k, a, b);
            if (black != (pass == 1))
               continue;
            const int note = baseNote + 12 * octave + k;
            const bool lit = ((n->uiKeys >> k) & 1ull) || (note >= 0 && note < 128 && n->midiNoteHeld[note]);
            const bool led = keyLeds >= 0 && n->Led(keyLeds + k) != 0;
            const ImU32 fill = lit ? SrAccent() : black ? IM_COL32(22, 22, 26, 255) : IM_COL32(236, 236, 232, 255);
            dl->AddRectFilled(a, b, fill, black ? 2.0f : 3.0f);
            dl->AddRect(a, b, IM_COL32(30, 30, 34, 255), black ? 2.0f : 3.0f);
            if (led)
               dl->AddRectFilled(ImVec2(a.x + 3, a.y + 3), ImVec2(b.x - 3, a.y + 7), IM_COL32(255, 70, 50, 255), 1.0f);
            if (!black && (baseNote + k) % 12 == 0)
            {
               char c[8];
               snprintf(c, sizeof(c), "C%d", note / 12 - 1);
               SrLabelCentered(dl, ImVec2((a.x + b.x) * 0.5f, b.y - 9), IM_COL32(90, 90, 96, 255), c, 0.75f);
            }
         }
      if (hit >= 0 && ImGui::IsItemHovered())
      {
         char v[32];
         snprintf(v, sizeof(v), "key %d (note %d)", hit + 1, baseNote + 12 * octave + hit);
         SetAudioReadout("keyboard", v);
      }
   }

   const char* SrControlName(const SynthRefCoreInfo* info, int kind, int id, const char* fallback)
   {
      if (info != nullptr)
         for (uint32_t i = 0; i < info->controlCount; ++i)
            if (info->controls[i].kind == kind && info->controls[i].id == id && info->controls[i].name)
               return info->controls[i].name;
      return fallback;
   }

   void SrMeter(SynthRefNode* n, SrUiState& ui, ImVec2 tl, ImVec2 size)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (int ch = 0; ch < 2; ++ch)
      {
         const float p = n->MeterPeak(ch);
         ui.peak[ch] = std::max(p, ui.peak[ch] * 0.92f);
         const float db = 20.0f * std::log10(std::max(ui.peak[ch], 1e-5f));
         const float t = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
         const float bw = (size.x - 2.0f) * 0.5f;
         const ImVec2 a(tl.x + ch * (bw + 2.0f), tl.y), b(a.x + bw, tl.y + size.y);
         dl->AddRectFilled(a, b, IM_COL32(18, 18, 22, 255), 2.0f);
         dl->AddRectFilled(ImVec2(a.x, b.y - size.y * t), b,
                           db > -1.0f ? IM_COL32(230, 70, 60, 255) : db > -9.0f ? IM_COL32(230, 200, 60, 255) : IM_COL32(90, 200, 120, 255), 2.0f);
      }
   }

   void SrHeader(GraphNode& gn, SynthRefNode* n)
   {
      // Firmware choice: Supported / Experimental are selectable; the rest
      // are listed so the user sees they are known, with why they don't run.
      const auto& list = SynthRefFirmwares();
      std::vector<std::string> names;
      int current = 0;
      for (size_t i = 0; i < list.size(); ++i)
      {
         std::string s = std::string(list[i].instrument) + "  |  " + list[i].firmware;
         if (list[i].status == SynthRefStatus::Experimental)
            s += "  (experimental)";
         if (list[i].status == SynthRefStatus::Unavailable)
            s += "  (unavailable)";
         names.push_back(s);
         if (n->coreId == list[i].coreId)
            current = (int)i;
      }
      DropdownButton("##srfw", names, current, [n, &list](int idx) {
         const SynthRefFirmware& f = list[(size_t)idx];
         if (f.status == SynthRefStatus::Unavailable)
         {
            n->uiMessage = std::string(f.firmware) + " is unavailable: " + f.note;
            return;
         }
         if (f.coreId != n->coreId)
         {
            PushUndoCheckpoint();
            n->SwitchCore(f.coreId);
            n->uiMessage = n->CoreReady() ? std::string("running ") + f.firmware : n->CoreError();
         }
      }, n->view == SynthRefNode::kViewFull ? 340.0f : 250.0f, false);
      ImGui::SameLine();
      const bool midi = n->MidiRecent();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 7, p.y + 10), 5.0f,
                                                  midi ? IM_COL32(80, 230, 120, 255) : IM_COL32(50, 70, 55, 255));
      ImGui::Dummy(ImVec2(16, 18));
      ImGui::SameLine();
      if (ImGui::SmallButton(n->view == SynthRefNode::kViewFull ? "COMPACT" : "FULL PANEL"))
      {
         PushUndoCheckpoint();
         n->view = n->view == SynthRefNode::kViewFull ? SynthRefNode::kViewCompact : SynthRefNode::kViewFull;
      }
      (void)gn;
   }

   void SrPresetRow(SynthRefNode* n)
   {
      const int track = n->Query(SR_Q_TRACK);
      const std::string source = track != SR_QUERY_NONE ? n->TrackSource(track) : std::string();
      ImGui::Text("T%d %s", track != SR_QUERY_NONE ? track + 1 : 0, source.c_str());
      ImGui::SameLine();
      const auto& factories = n->FactoryOptions();
      if (!factories.empty())
      {
         DropdownButton("##srfactory", factories, 0, [n](int idx) {
            if (idx > 0)
               n->uiMessage = n->LoadFactory(idx - 1) == 0 ? "factory project loaded" : "factory project refused";
         }, 170.0f, false);
         ImGui::SameLine();
      }
      const auto& programs = n->ProgramOptions();
      const int program = n->Query(SR_Q_PROGRAM);
      if (!programs.empty())
      {
         if (program < 0)
            ImGui::BeginDisabled();
         DropdownButton("##srprogram", programs, std::clamp(program, 0, (int)programs.size() - 1), [n](int idx) {
            n->uiMessage = n->LoadProgram(idx) == 0 ? "DX7 program loaded on the selected track" : "program refused";
         }, 190.0f, false);
         if (program < 0)
         {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               SetAudioReadout("DX7 library", "select a DX7 track to choose its program");
         }
         ImGui::SameLine();
      }
      if (ImGui::SmallButton("LOAD FILE"))
      {
         const std::string path = SynthRefOpenFileDialog(L"Load into Synth REF",
                                                         L"Synth REF files (*.syx;*.fm1;*.fm1t)\0*.syx;*.fm1;*.fm1t\0All files\0*.*\0\0",
                                                         SynthRefUserDir());
         if (!path.empty())
         {
            std::string msg;
            // A 32-voice bank goes into library bank 4 (programs 97-128), the
            // user bank slot, leaving the three factory groups in place.
            n->ImportFile(path, 3, msg);
            n->uiMessage = msg;
         }
      }
      if (ImGui::IsItemHovered())
         SetAudioReadout("load file", ".syx DX7 voice / 32-voice bank, .fm1 project, .fm1t timbre");
      ImGui::SameLine();
      if (ImGui::SmallButton("SAVE PROJECT"))
      {
         const std::string path = SynthRefSaveFileDialog(L"Save the firmware project", L"Project (*.fm1)\0*.fm1\0\0", L"fm1",
                                                         SynthRefUserDir());
         if (!path.empty())
         {
            std::string msg;
            n->ExportProject(path, msg);
            n->uiMessage = msg;
         }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("BANKS FOLDER"))
         SynthRefRevealFolder(SynthRefUserDir());
      ImGui::SameLine();
      if (ImGui::SmallButton("ROMS"))
      {
         // Firmware ROMs (.wasm, as their authors distribute them): open the
         // folder and list what is in it now in the firmware menu.
         SynthRefRescanRoms();
         SynthRefRevealFolder(SynthRefUserDir() + "\\roms");
      }
      if (ImGui::IsItemHovered())
         SetAudioReadout("ROMS", "drop firmware ROMs (.wasm simulator builds) here; they appear in the firmware menu");
      ImGui::SameLine();
      if (ImGui::SmallButton("PANIC"))
         n->Panic();
      if (ImGui::IsItemHovered())
         SetAudioReadout("panic", "all notes off, every held control released");
   }

   void SrMidiRow(SynthRefNode* n, SrUiState& ui)
   {
      // Device: every open input, plus the saved one while it is unplugged.
      if (ImGui::GetTime() - ui.devicesAt > 1.0)
      {
         ui.devices = Platform::MidiDevices();
         ui.devicesAt = ImGui::GetTime();
      }
      const auto& devices = ui.devices;
      std::vector<std::string> opts { "(no MIDI device)" };
      int current = 0;
      bool savedListed = n->deviceKey.empty();
      for (const auto& d : devices)
      {
         opts.push_back(d.key);
         if (d.key == n->deviceKey)
         {
            current = (int)opts.size() - 1;
            savedListed = true;
         }
      }
      if (!savedListed)
      {
         opts.push_back(n->deviceKey + "  (not connected)");
         current = (int)opts.size() - 1;
      }
      ImGui::TextUnformatted("MIDI");
      ImGui::SameLine();
      DropdownButton("##srdev", opts, current, [n, uiPtr = &ui](int idx) {
         PushUndoCheckpoint();
         if (idx == 0)
            n->deviceKey.clear();
         else if (idx - 1 < (int)uiPtr->devices.size())
            n->deviceKey = uiPtr->devices[(size_t)idx - 1].key;
         n->Focus();
      }, 210.0f, false);
      ImGui::SameLine();
      bool lock = n->deviceLock;
      if (ImGui::Checkbox("lock", &lock))
      {
         PushUndoCheckpoint();
         n->deviceLock = lock;
      }
      if (ImGui::IsItemHovered())
         SetAudioReadout("lock", "on: always plays from this device; off: plays while this Synth REF is the selected one");
      ImGui::SameLine();
      if (ImGui::SmallButton("RESCAN"))
      {
         n->Rescan();
         ui.devicesAt = -10.0;
      }
      if (ImGui::IsItemHovered())
         SetAudioReadout("MIDI DEVICE RESCAN", "reopen every MIDI input (a controller plugged in later)");
      ImGui::SameLine();
      const bool autoMap = n->mapMode == SynthRefNode::kMapAuto;
      if (ImGui::RadioButton("AUTO MAP", autoMap) && !autoMap)
      {
         PushUndoCheckpoint();
         n->mapMode = SynthRefNode::kMapAuto;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("MIDI LEARN", !autoMap) && autoMap)
      {
         PushUndoCheckpoint();
         n->mapMode = SynthRefNode::kMapLearn;
      }
      if (ImGui::IsItemHovered())
         SetAudioReadout("MIDI LEARN", "encoders, faders and pads below become parameters: right-click one to learn it");
      ImGui::SameLine();
      std::vector<std::string> profiles { "auto profile" };
      std::vector<std::string> ids { "" };
      int pcur = 0;
      for (const SrProfile& p : n->Profiles())
         if (p.core == n->coreId)
         {
            profiles.push_back(p.name.empty() ? p.id : p.name);
            ids.push_back(p.id);
            if (p.id == n->profileId)
               pcur = (int)ids.size() - 1;
         }
      DropdownButton("##srprofile", profiles, pcur, [n, ids](int idx) {
         PushUndoCheckpoint();
         n->profileId = ids[(size_t)idx];
      }, 200.0f, false);

      // Status line: what is connected, which profile is live, last event.
      const SrProfile* prof = n->ActiveProfile();
      const bool active = n->DeviceActive();
      char status[256];
      snprintf(status, sizeof(status), "%s  |  %s  |  %s%s",
               n->deviceKey.empty() ? "no device" : (n->DeviceStatus().empty() ? "checking..." : n->DeviceStatus().c_str()),
               n->noteInput.IsConnected() ? "notes from the pin" : (active ? "playing from the device" : "device idle (select this node or lock)"),
               n->mapMode == SynthRefNode::kMapAuto ? (prof ? prof->name.c_str() : "no profile for this device") : "MIDI LEARN",
               n->LastEvent().empty() ? "" : ("  |  " + n->LastEvent()).c_str());
      ImGui::TextDisabled("%s", status);
   }

   // The MIDI LEARN parameters are registered every frame in the same order,
   // after "volume", in both views and both modes: a parameter only exists in
   // the frames it registers, and its index is its registration order, so
   // learned mappings and cables survive COMPACT / AUTO MAP. Hidden, they use
   // the collapsed-node pass (register only, no items, nothing drawn).
   void SrLearnStrip(SynthRefNode* n, bool visible)
   {
      ImGuiWindow* window = ImGui::GetCurrentWindow();
      const bool savedSkip = window->SkipItems, savedRegister = gParamRegisterOnly;
      if (!visible)
      {
         gParamRegisterOnly = true;
         window->SkipItems = true;
         ImGui::GetWindowDrawList()->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), false);
      }
      const SynthRefCoreInfo* info = n->Info();
      if (visible)
         ImGui::TextDisabled("MIDI LEARN: right-click a control to learn it, or patch a modulator");
      AudioKnobRow row(SynthRefNode::kLearnKnobs + SynthRefNode::kLearnFaders, kKnobSmall);
      static const char* fallbackK[8] = { "K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8" };
      static const char* fallbackF[4] = { "F1", "F2", "F3", "F4" };
      for (int i = 0; i < SynthRefNode::kLearnKnobs; ++i)
         row.Knob(SrControlName(info, SR_CTL_ENCODER, i, fallbackK[i]), &n->learnKnob[i], 0.0f, 1.0f, "%.2f", kKnobSmall);
      for (int i = 0; i < SynthRefNode::kLearnFaders; ++i)
         row.Knob(SrControlName(info, SR_CTL_FADER, i, fallbackF[i]), &n->learnFader[i], 0.0f, 1.0f, "%.2f", kKnobSmall);
      row.End();
      const float gap = 4.0f, w = (gAudioContentW - gap * 15.0f) / 16.0f;
      for (int p = 0; p < SynthRefNode::kLearnPads; ++p)
      {
         if (p > 0)
            ImGui::SameLine(0.0f, gap);
         char label[32];
         snprintf(label, sizeof(label), "P%d##srlearnpad%d", p + 1, p);
         bool clicked = false;
         n->learnPadHeld[p] = ModGateButton(label, ImVec2(w, 30.0f), n->learnPadHeld[p], false, true, "", clicked);
      }
      if (!visible)
      {
         ImGui::GetWindowDrawList()->PopClipRect();
         window->SkipItems = savedSkip;
         gParamRegisterOnly = savedRegister;
      }
   }

   void SrDiagnostics(SynthRefNode* n, SrUiState& ui)
   {
      BeginAudioSection("MIDI DIAGNOSTICS");
      const auto& devices = ui.devices;
      std::string list;
      for (const auto& d : devices)
         list += (list.empty() ? "" : ", ") + d.key;
      ImGui::TextWrapped("Devices: %s", list.empty() ? "none open (RESCAN)" : list.c_str());
      if (const SrProfile* p = n->ActiveProfile())
      {
         ImGui::TextDisabled("Profile %s (%s)%s%s", p->name.c_str(), p->file.c_str(), p->mode.empty() ? "" : ", mode: ", p->mode.c_str());
         for (const std::string& l : p->lines)
            ImGui::TextDisabled("  %s", l.c_str());
      }
      for (const std::string& e : n->ProfileErrors())
         ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "profile error: %s", e.c_str());
      const auto& log = n->DiagLog();
      const size_t first = log.size() > 14 ? log.size() - 14 : 0;
      for (size_t i = first; i < log.size(); ++i)
         ImGui::TextUnformatted(log[i].c_str());
      if (log.empty())
         ImGui::TextDisabled("no messages yet: play a key or move a knob on the selected device");
      if (ImGui::SmallButton("clear"))
         n->ClearDiag();
      ImGui::SameLine();
      if (ImGui::SmallButton("reload profiles"))
         n->ReloadProfiles();
      ImGui::TextWrapped(
         "Nothing arrives: check the USB cable carries data, pick the device above, press RESCAN, close other apps using it. "
         "Keys play but knobs don't: the MiniLab must be in its ARTURIA or USER program (not DAW) with the factory CCs "
         "(see the profile lines; any channel), or use MIDI LEARN. Pads wrong: pads bank A = notes 36-43, bank B = 44-51 "
         "on channel 10; keys play on any channel. "
         "Still stuck: Arturia's factory reset (manual, see docs/SYNTH-REF-MVP-TEST-GUIDE.md); Synth REF never resets or "
         "flashes a controller.");
      EndAudioSection();
   }

   void DrawSynthRefBody(GraphNode& gn, SynthRefNode* n)
   {
      SrUiState& ui = SrUi(n);
      if (gEditor != nullptr && ed::IsNodeSelected(gn.NodeId()))
         n->Focus();
      const SynthRefCoreInfo* info = n->Info();
      char stat[160];
      if (info != nullptr)
         snprintf(stat, sizeof(stat), "%s %s  |  %s  |  %s%s", info->manufacturer, info->model, info->coreName, n->ProjectName().c_str(),
                  n->AudioLive() ? "" : "  |  audio off: the firmware runs silently");
      else
         snprintf(stat, sizeof(stat), "firmware not running: %s", n->CoreError().c_str());
      const bool full = n->view == SynthRefNode::kViewFull;
      BeginAudioBody(gn.index, gn.category, full ? kAudioWideWidth : kAudioNodeWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      SrHeader(gn, n);
      info = n->Info(); // the header may have switched the firmware (old DLL unloaded)
      if (!n->CoreError().empty())
         ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", n->CoreError().c_str());
      if (!n->uiMessage.empty())
         ImGui::TextDisabled("%s", n->uiMessage.c_str());

      if (!full)
      {
         // COMPACT: the live screen small, project and track, meter, volume.
         const ImVec2 o = ImGui::GetCursorScreenPos();
         SrDrawScreen(n, ImVec2(o.x + 4, o.y + 4), 120.0f);
         const float x = o.x + 140.0f;
         ImGui::SetCursorScreenPos(ImVec2(x, o.y + 2));
         ImGui::Text("%s", n->ProjectName().c_str());
         const int track = n->Query(SR_Q_TRACK);
         const int program = n->Query(SR_Q_PROGRAM);
         ImGui::SetCursorScreenPos(ImVec2(x, o.y + 22));
         if (track != SR_QUERY_NONE)
         {
            if (program >= 0 && program < (int)n->ProgramNames().size())
               ImGui::TextDisabled("T%d %s  %03d %s", track + 1, n->TrackSource(track).c_str(), program + 1, n->ProgramNames()[(size_t)program].c_str());
            else
               ImGui::TextDisabled("T%d %s", track + 1, n->TrackSource(track).c_str());
         }
         ImGui::SetCursorScreenPos(ImVec2(x, o.y + 42));
         ImGui::TextDisabled("%s", n->noteInput.IsConnected() ? "notes: pin" : n->deviceKey.empty() ? "notes: no device" : n->deviceKey.c_str());
         SrMeter(n, ui, ImVec2(x + 230.0f, o.y + 4), ImVec2(14.0f, 116.0f));
         ImGui::SetCursorScreenPos(ImVec2(x, o.y + 64));
         AudioKnobRow row(1, kKnobLarge);
         row.x0 = x;
         row.cellW = 90.0f;
         row.Knob("volume", &n->volume, -60.0f, 12.0f, "%.1f dB", kKnobLarge);
         row.End();
         ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + 132));
         ImGui::Dummy(ImVec2(gAudioContentW, 2));
         SrLearnStrip(n, false);
         EndAudioBody();
         return;
      }

      // FULL: screen | buttons, encoders, faders / wheels / pots, meter, then
      // pads and keys. Built from the controls the core declares, so every
      // instrument gets its own panel (SMK-37: 18 buttons, 8 encoders, 4
      // faders + 2 wheels + pedal, 16 pads, 37 keys; FM-1: 14 buttons, 7
      // encoders, a volume pot, 27 keys with LEDs).
      int buttons = 0, encoders = 0, faders = 0, pads = 0, keys = 0;
      bool hasPedal = false;
      for (uint32_t i = 0; info != nullptr && i < info->controlCount; ++i)
      {
         const SynthRefControlInfo& c = info->controls[i];
         const int top = (int)c.id + 1;
         if (c.kind == SR_CTL_BUTTON) buttons = std::max(buttons, top);
         else if (c.kind == SR_CTL_ENCODER) encoders = std::max(encoders, top);
         else if (c.kind == SR_CTL_FADER) faders = std::max(faders, top);
         else if (c.kind == SR_CTL_PAD) pads = std::max(pads, top);
         else if (c.kind == SR_CTL_KEY) keys = std::max(keys, top);
         else if (c.kind == SR_CTL_PEDAL) hasPedal = true;
      }
      buttons = std::min(buttons, 32);
      encoders = std::min(encoders, 8);
      faders = std::min(faders, 6);
      pads = std::min(pads, 16);
      int ledCount = n->Query(SR_Q_LED_COUNT);
      if (ledCount == SR_QUERY_NONE)
         ledCount = 0;
      const int keyLeds = ledCount > buttons ? buttons : -1;
      const int padLeds = ledCount > buttons + keys ? buttons + keys : -1;
      const int baseNote = info != nullptr && info->structSize >= sizeof(SynthRefCoreInfo) ? info->keyboardBaseNote : 48;
      // A pot / fader starts where the core says it rests (FM-1 volume at max).
      if (ui.faderCore != (info ? info->coreId : ""))
      {
         ui.faderCore = info ? info->coreId : "";
         for (uint32_t i = 0; info != nullptr && i < info->controlCount; ++i)
            if (info->controls[i].kind == SR_CTL_FADER && info->controls[i].id < 6)
               n->uiFader[info->controls[i].id] = info->controls[i].restValue;
      }

      const ImVec2 o = ImGui::GetCursorScreenPos();
      const float W = gAudioContentW;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(o, ImVec2(o.x + W, o.y + 262), IM_COL32(30, 31, 36, 255), 8.0f);
      SrDrawScreen(n, ImVec2(o.x + 12, o.y + 11), 240.0f);
      const float rx = o.x + 272.0f, rw = W - 280.0f;
      const int perRow = std::max(1, (buttons + 1) / 2);
      for (int b = 0; b < buttons; ++b)
      {
         const int row = b / perRow, col = b % perRow;
         const float bw = (rw - (perRow - 1) * 5.0f) / perRow;
         // LEDs come one per button id, then per key, then per pad (ABI).
         SrButton(n, ui, b, SrControlName(info, SR_CTL_BUTTON, b, "?"), ImVec2(rx + col * (bw + 5.0f), o.y + 12 + row * 36.0f),
                  ImVec2(bw, 30.0f), b < ledCount && n->Led(b) != 0);
      }
      const float encY = o.y + 112.0f;
      for (int e = 0; e < encoders; ++e)
         SrEncoder(n, ui, e, SrControlName(info, SR_CTL_ENCODER, e, "?"), ImVec2(rx + 24.0f + e * 52.0f, encY), 19.0f);
      const int pickup = n->Query(SR_Q_PICKUP_MASK);
      const int bank = n->Query(SR_Q_FADER_BANK);
      const float fx = rx + encoders * 52.0f + 12.0f;
      for (int f = 0; f < faders; ++f)
      {
         int rest = 0;
         for (uint32_t i = 0; info != nullptr && i < info->controlCount; ++i)
            if (info->controls[i].kind == SR_CTL_FADER && info->controls[i].id == f)
               rest = info->controls[i].restValue;
         const char* base = SrControlName(info, SR_CTL_FADER, f, "?");
         char name[24];
         // Fader banks (SMK-37): F1..F4 become F5..F8.
         if (bank == 1 && base[0] == 'F' && base[1] >= '1' && base[1] <= '4' && base[2] == 0)
            snprintf(name, sizeof(name), "F%d", base[1] - '0' + 4);
         else
            snprintf(name, sizeof(name), "%s", base);
         const bool spring = rest == 64; // a pitch wheel returns to centre
         SrFader(n, ui, f, name, ImVec2(fx + f * 30.0f + (spring || f >= 4 ? 12.0f : 0.0f), o.y + 84), ImVec2(24.0f, 150.0f),
                 rest, spring, pickup != SR_QUERY_NONE && f < 4 && ((pickup >> f) & 1));
      }
      SrMeter(n, ui, ImVec2(o.x + W - 26.0f, o.y + 84), ImVec2(16.0f, 150.0f));
      // Sustain pedal (when the instrument has one) and volume under the encoders.
      if (hasPedal)
      {
         ImGui::SetCursorScreenPos(ImVec2(rx, o.y + 150.0f));
         bool pedal = n->uiPedal;
         ImGui::PushID("srpedal");
         ImGui::Button(pedal ? "SUSTAIN (down)" : "SUSTAIN", ImVec2(120, 24));
         const bool pedalHeld = ImGui::IsItemActive();
         ImGui::PopID();
         if (pedalHeld != pedal && (pedalHeld || ImGui::IsItemDeactivated()))
         {
            n->uiPedal = pedalHeld;
            n->SendControl(SR_CTL_PEDAL, 0, pedalHeld ? 127 : 0);
         }
      }
      ImGui::SetCursorScreenPos(ImVec2(rx, o.y + 182.0f));
      {
         AudioKnobRow row(1, kKnobSmall);
         row.x0 = rx;
         row.cellW = 80.0f;
         row.Knob("volume", &n->volume, -60.0f, 12.0f, "%.1f dB", kKnobSmall);
         row.End();
      }
      const int tempo = n->Query(SR_Q_TEMPO_X10);
      const int keyCh = n->Query(SR_Q_KEY_CHANNEL);
      ImGui::SetCursorScreenPos(ImVec2(rx + 100.0f, o.y + 190.0f));
      if (tempo != SR_QUERY_NONE)
         ImGui::TextDisabled("tempo %.1f  |  keyboard on ch %d%s", tempo / 10.0, keyCh + 1,
                             pads == 0                         ? ""
                             : n->Query(SR_Q_SHIFT) == 1       ? "  |  SHIFT held"
                             : n->Query(SR_Q_PAD_STEPS) == 1   ? "  |  pads: steps"
                                                               : "  |  pads: play");

      // Pads (when the instrument has them), keyboard.
      float y = o.y + 274.0f;
      if (pads > 0)
      {
         SrPads(n, ui, ImVec2(o.x + 12.0f, y), W - 24.0f, padLeds);
         y += 92.0f;
      }
      if (keys > 0)
      {
         SrKeyboard(n, ui, ImVec2(o.x + 12.0f, y), W - 24.0f, 96.0f, keys, baseNote, keyLeds);
         y += 104.0f;
      }
      ImGui::SetCursorScreenPos(ImVec2(o.x, y));
      ImGui::Dummy(ImVec2(W, 2));

      SrPresetRow(n);
      SrMidiRow(n, ui);
      SrLearnStrip(n, n->mapMode == SynthRefNode::kMapLearn);
      if (ImGui::SmallButton(n->showDiag ? "hide MIDI DIAGNOSTICS" : "MIDI DIAGNOSTICS"))
         n->showDiag = !n->showDiag;
      if (n->showDiag)
         SrDiagnostics(n, ui);
      EndAudioBody();
   }

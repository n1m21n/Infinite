// Part of src/main.cpp, included there in order (one translation unit).
// Turbo 0.73: the Circuit Synth body - preset, generator and play mode on
// top, a stereo scope, one knob per part of the circuit, then play, LFO and
// stereo sections.

   const std::vector<std::string>& CircuitPresetNames()
   {
      static const std::vector<std::string> names = []
      {
         std::vector<std::string> v;
         for (int i = 0; i < CircuitDsp::NumPresets(); i++)
            v.push_back(CircuitDsp::GetPreset(i).name);
         return v;
      }();
      return names;
   }

   std::vector<std::string> CircuitProbeNames(const CircuitDsp::Preset& pr)
   {
      std::vector<std::string> v;
      for (int i = 0; i < pr.numProbes; i++)
         v.push_back(pr.probes[i].name);
      return v;
   }

   // The last ~20 ms of L (cyan) and R (amber) over each other: two traces
   // that coincide are mono, two that slide against each other are the
   // twins and probes at work. Also returns the window's L/R correlation.
   float DrawCircuitScope(CircuitSynthNode* n, float h, float width)
   {
      const double now = ImGui::GetTime();
      if (n->scopeTime < 0.0 || now - n->scopeTime > 1.0 / 30.0)
      {
         float buf[CircuitSynthNode::kScopeCapacity * 4];
         const int cap = (int)(sizeof(buf) / sizeof(buf[0]));
         int count = 0, got;
         while (count < cap && (got = n->ReadScope(buf + count, cap - count)) > 0)
            count += got;
         count &= ~1;
         if (count >= 4)
         {
            const int keep = std::min(count, CircuitSynthNode::kScopeCapacity);
            std::copy(buf + count - keep, buf + count, n->scopeCache);
            n->scopeCount = keep;
         }
         n->scopeTime = now;
      }

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);
      const float midY = origin.y + h * 0.5f;
      for (int i = 1; i < 8; i++)
         dl->AddLine(ImVec2(origin.x + w * i / 8.0f, origin.y), ImVec2(origin.x + w * i / 8.0f, br.y), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      float corr = 1.0f;
      const int pairs = n->scopeCount / 2;
      double peak = 0.0;
      for (int i = 0; i < pairs * 2; i++)
         peak = std::max(peak, (double)std::fabs(n->scopeCache[i]));
      if (pairs > 1 && peak > 1e-4)
      {
         double sl = 0.0, sr = 0.0, slr = 0.0;
         for (int i = 0; i < pairs; i++)
         {
            const double l = n->scopeCache[2 * i], r = n->scopeCache[2 * i + 1];
            sl += l * l;
            sr += r * r;
            slr += l * r;
         }
         corr = (float)(slr / std::sqrt(sl * sr + 1e-30));
         const float scale = (float)(h * 0.44 / std::max(0.25, peak)); // small signals still fill the view
         const bool isLight = IsThemeLight();
         for (int ch = 1; ch >= 0; ch--)
         {
            dl->PathClear();
            for (int i = 0; i < pairs; i++)
               dl->PathLineTo(ImVec2(origin.x + w * (float)i / (float)(pairs - 1), midY - n->scopeCache[2 * i + ch] * scale));
            const ImU32 col = ch == 0 ? (isLight ? IM_COL32(0, 130, 200, 240) : IM_COL32(80, 220, 255, 235))
                                      : (isLight ? IM_COL32(210, 120, 10, 220) : IM_COL32(255, 190, 80, 210));
            dl->PathStroke(col, 0, 1.5f);
         }
         char txt[32];
         snprintf(txt, sizeof(txt), "L/R %.2f", corr);
         dl->AddText(ImVec2(br.x - ImGui::CalcTextSize(txt).x - 6.0f, origin.y + 3.0f), ScopeTextCol(), txt);
      }
      else
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "tap / play a note");
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
      return corr;
   }

   void DrawCircuitSynthBody(GraphNode& gn, CircuitSynthNode* n)
   {
      const CircuitDsp::Preset& pr = CircuitDsp::GetPreset(n->preset);
      const int voices = n->ActiveVoices();
      char stat[128];
      if (n->mode == CircuitDsp::kPoly)
         snprintf(stat, sizeof(stat), "%s  -  poly %d/%d", pr.name, voices, n->MaxVoices());
      else
         snprintf(stat, sizeof(stat), "%s  -  mono%s", pr.name, voices > 0 ? "  -  playing" : "");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Header: circuit, generator wave, mono / poly, tap / hold.
      {
         static const std::vector<std::string> kWaves = { "saw", "pulse", "sine", "noise", "step" };
         static const std::vector<std::string> kModes = { "mono", "poly" };
         static const std::vector<std::string> kTriggers = { "tap", "hold" };
         const float w = gAudioContentW, x0 = gAudioContentX, y = ImGui::GetCursorScreenPos().y, gap = 5.0f;
         const float waveW = 72.0f, modeW = 64.0f, trigW = 60.0f;
         const float presetW = std::max(90.0f, w - waveW - modeW - trigW - gap * 3.0f);
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("csPreset", CircuitPresetNames(), n->preset, [n](int i) { PushUndoCheckpoint(); n->ApplyPreset(i); }, presetW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + presetW + gap, y));
         AudioBareDropdown("csWave", kWaves, n->wave, [n](int i) { PushUndoCheckpoint(); n->wave = i; }, waveW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + presetW + waveW + gap * 2.0f, y));
         AudioBareDropdown("csMode", kModes, n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; }, modeW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + w - trigW, y));
         AudioBareDropdown("csTrigger", kTriggers, n->trigger, [n](int i) { PushUndoCheckpoint(); n->trigger = i; }, trigW);
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Stereo scope and the TAP button.
      {
         const float w = gAudioContentW, tapW = 64.0f, scopeH = 62.0f;
         const ImVec2 pos = ImGui::GetCursorScreenPos();
         DrawCircuitScope(n, scopeH, w - tapW - 6.0f);
         ImGui::SetCursorScreenPos(ImVec2(pos.x + w - tapW, pos.y));
         if (ModTriggerButton("TAP##circuitTap", ImVec2(tapW, scopeH)))
            n->Tap();
         ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + scopeH));
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // One knob per part (or group of parts). The caption is the part's
      // name; the readout shows its value. Always two rows, so switching to
      // a circuit with fewer parts never resizes the node.
      BeginAudioSection("parts");
      for (int rowStart = 0; rowStart < CircuitSynthNode::kKnobs; rowStart += 4)
      {
         AudioKnobRow row(4);
         for (int k = rowStart; k < rowStart + 4; k++)
         {
            if (k >= pr.numKnobs || pr.knobs[k].part < 0)
            {
               row.index++;
               continue;
            }
            const CircuitDsp::PartDef& part = pr.parts[pr.knobs[k].part];
            char value[48], fmt[96];
            CircuitDsp::FormatPartValue(part, CircuitDsp::KnobScale(part.kind, n->knob[k]), value, sizeof(value));
            int o = 0;
            for (const char* c = value; *c && o < (int)sizeof(fmt) - 2; c++)
            {
               if (*c == '%')
                  fmt[o++] = '%';
               fmt[o++] = *c;
            }
            fmt[o] = 0;
            row.Knob(pr.knobs[k].label, &n->knob[k], -1.0f, 1.0f, fmt, k < 2 ? kKnobLarge : kKnobSmall);
         }
         row.End();
      }
      EndAudioSection();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      BeginAudioSection("play");
      {
         AudioKnobRow row(5);
         row.Knob("pitch", &n->pitch, -24.0f, 24.0f, "%+.1f st");
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s");
         row.Knob("contact", &n->contactMs, 0.05f, 50.0f, "%.2f ms");
         row.Knob("attack", &n->attack, 0.0005f, 2.0f, "%.3f s");
         row.Knob("release", &n->release, 0.01f, 8.0f, "%.2f s");
         row.End();
      }
      AudioToggleButton("C key##circuitCFollow", &n->cFollow, 64.0f);
      ImGui::SameLine();
      AudioToggleButton("L key##circuitLFollow", &n->lFollow, 64.0f);
      EndAudioSection();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      BeginAudioSection("lfo");
      {
         static const std::vector<std::string> kShapes = { "sine", "triangle", "square", "random" };
         static const std::vector<std::string> kTargets = { "tune", "power", "damp", "level" };
         AudioKnobRow row(4);
         row.Knob("rate", &n->lfoRate, 0.05f, 20.0f, "%.2f Hz");
         row.Knob("depth", &n->lfoDepth, 0.0f, 1.0f, "%.2f");
         row.Dropdown("shape", kShapes, n->lfoShape, [n](int i) { PushUndoCheckpoint(); n->lfoShape = i; });
         row.Dropdown("target", kTargets, n->lfoTarget, [n](int i) { PushUndoCheckpoint(); n->lfoTarget = i; });
         row.End();
      }
      EndAudioSection();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      BeginAudioSection("stereo");
      {
         const std::vector<std::string> probes = CircuitProbeNames(pr);
         AudioKnobRow row(5);
         row.Dropdown("probe A", probes, n->probeA, [n](int i) { PushUndoCheckpoint(); n->probeA = i; });
         row.Dropdown("probe B", probes, n->probeB, [n](int i) { PushUndoCheckpoint(); n->probeB = i; });
         row.Knob("width", &n->width, 0.0f, 1.5f, "%.2f");
         row.Knob("spread", &n->spread, 0.0f, 5.0f, "%.1f %%");
         row.Knob("level", &n->level, 0.0f, 2.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }

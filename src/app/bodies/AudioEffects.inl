// Part of src/main.cpp, included there in order (one translation unit).
// audio effect node bodies

   // ---- Resonator Bank -------------------------------------------------
   // Log-frequency pole-distribution visualizer: stems showing each active
   // resonator's center frequency, Q height and stereo pan coloring.
   void DrawResonatorBankVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float logMin = log10f(20.0f), logMax = log10f(20000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(std::max(hz, 20.0f)) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };

      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
         char hzStr[16];
         if (hz >= 1000.0f)
            snprintf(hzStr, sizeof(hzStr), "%.0fk", hz / 1000.0f);
         else
            snprintf(hzStr, sizeof(hzStr), "%.0f", hz);
         dl->AddText(ImVec2(x + 2.0f, br.y - 12.0f), ScopeTextCol(), hzStr);
      }

      const float rootFreq = std::clamp(n->Param("rootFreq"), 20.0f, 2000.0f);
      const int structure = std::clamp((int)std::round(n->Param("structure")), 0, 3);
      const int numPoles = std::clamp((int)std::round(n->Param("poles")), 1, 16);
      const float decay = std::clamp(n->Param("decay"), 0.05f, 10.0f);
      const float spread = std::clamp(n->Param("spread"), 0.0f, 1.0f);
      const float damp = std::clamp(n->Param("damp"), 0.0f, 1.0f);

      for (int i = 0; i < numPoles; i++)
      {
         float f_i = rootFreq;
         switch (structure)
         {
            case 0: f_i = rootFreq * (float)(i + 1); break;
            case 1: f_i = rootFreq * (float)(2 * i + 1); break;
            case 2: {
               static const int kChordSet[6] = { 0, 7, 12, 16, 19, 24 };
               f_i = rootFreq * powf(2.0f, (float)(kChordSet[i % 6] + 12 * (i / 6)) / 12.0f);
               break;
            }
            case 3:
            default:
               f_i = rootFreq * (float)(i + 1) * sqrtf(1.0f + 0.001f * (float)((i + 1) * (i + 1)));
               break;
         }
         if (f_i > 20000.0f)
            continue;

         const float x = freqToX(f_i);
         // Mirrors ResonatorBankKernel::PushParams' decay_i - see the comment
         // there for the formula and why damp == 0 is a no-op.
         const float freqRatio = std::min(rootFreq / f_i, 1.0f);
         const float decay_i = std::clamp(decay * powf(freqRatio, damp), 0.05f, decay);
         const float q_i = std::clamp(0.4547f * f_i * decay_i, 0.5f, 500.0f);
         const float normH = std::clamp(log10f(q_i) / log10f(500.0f), 0.1f, 0.9f);
         const float peakY = br.y - 14.0f - normH * (h - 22.0f);
         // Damped partials fade as well as shrink, so raising damp visibly
         // tilts the whole spectrum down and out to the right.
         const float alphaScale = std::clamp(decay_i / std::max(decay, 0.0001f), 0.25f, 1.0f);

         const float pos = (numPoles > 1) ? ((float)i / (float)(numPoles - 1) - 0.5f) * spread + 0.5f : 0.5f;
         const ImU32 baseCol = (pos < 0.45f)
            ? (isLight ? IM_COL32(30, 110, 230, 220) : IM_COL32(110, 200, 255, 220))
            : ((pos > 0.55f)
               ? (isLight ? IM_COL32(230, 90, 40, 220) : IM_COL32(255, 140, 90, 220))
               : (isLight ? IM_COL32(40, 180, 110, 220) : IM_COL32(110, 230, 160, 220)));
         const unsigned baseAlpha = (baseCol >> IM_COL32_A_SHIFT) & 0xFF;
         const ImU32 stemCol = (baseCol & ~IM_COL32_A_MASK) |
            (((unsigned)(baseAlpha * alphaScale)) << IM_COL32_A_SHIFT);

         dl->AddLine(ImVec2(x, br.y - 14.0f), ImVec2(x, peakY), stemCol, 2.0f);
         dl->AddCircleFilled(ImVec2(x, peakY), 3.0f, stemCol);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         static const char* kStructNames[] = { "Harmonic", "Odd", "Chord", "Metallic" };
         char buf[80];
         if (damp > 0.001f)
            snprintf(buf, sizeof(buf), "root %.0f Hz - %s - %d poles - %.0f%% damp", rootFreq, kStructNames[structure], numPoles, damp * 100.0f);
         else
            snprintf(buf, sizeof(buf), "root %.0f Hz - %s - %d poles", rootFreq, kStructNames[structure], numPoles);
         SetAudioReadout("resonator bank", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawResonatorBankBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float rootFreq = n->Param("rootFreq");
      const int structure = std::clamp((int)std::round(n->Param("structure")), 0, 3);
      const int poles = std::clamp((int)std::round(n->Param("poles")), 1, 16);
      const float decay = n->Param("decay");
      const bool analog = n->Param("analog") != 0.0f;
      const float damp = std::clamp(n->Param("damp"), 0.0f, 1.0f);

      static const char* kStructNames[] = { "Harmonic", "Odd", "Chord", "Metallic" };
      char stat[96];
      if (damp > 0.001f)
         snprintf(stat, sizeof(stat), "%.0f Hz - %s - %d poles - %.0f%% damp%s", rootFreq, kStructNames[structure], poles, damp * 100.0f, analog ? " - analog" : "");
      else
         snprintf(stat, sizeof(stat), "%.0f Hz - %s - %d poles%s", rootFreq, kStructNames[structure], poles, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawResonatorBankVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4, kKnobLarge);
         static const std::vector<std::string> kStructs = { "harmonic", "odd", "chord", "metallic" };
         row.Dropdown("struct", kStructs, structure, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("structure") = (float)i;
         });
         row.Knob("root", n->ParamPtr("rootFreq"), 20.0f, 2000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("poles", n->ParamPtr("poles"), 1.0f, 16.0f, "%.0f", kKnobLarge);
         row.Knob("decay", n->ParamPtr("decay"), 0.05f, 10.0f, "%.2fs", kKnobLarge);
         row.End();
      }

      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("scatter", n->ParamPtr("scatter"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("damp", n->ParamPtr("damp"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         bool analogBool = analog;
         if (AudioToggleButton("analog##resonatorAnalog", &analogBool, 64.0f))
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
      }

      EndAudioBody();
   }

   // ---- Cycle Shaper ---------------------------------------------------
   // Single-wavecycle replacement visualizer: shows the synthesized geometric
   // waveform (Sine / Square / Triangle) within the gate threshold bounds.
   void DrawCycleShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeGridCol(), 1.0f);

      const int waveform = std::clamp((int)std::round(n->Param("waveform")), 0, 2);
      const float thresholdDb = std::clamp(n->Param("threshold"), -60.0f, -12.0f);
      const float threshLinear = powf(10.0f, thresholdDb / 20.0f);
      const float threshY1 = midY - threshLinear * (h * 0.45f);
      const float threshY2 = midY + threshLinear * (h * 0.45f);
      const ImU32 threshCol = isLight ? IM_COL32(180, 80, 80, 100) : IM_COL32(230, 90, 90, 100);
      dl->AddLine(ImVec2(origin.x, threshY1), ImVec2(br.x, threshY1), threshCol, 1.0f);
      dl->AddLine(ImVec2(origin.x, threshY2), ImVec2(br.x, threshY2), threshCol, 1.0f);

      const ImU32 waveCol = isLight ? IM_COL32(20, 140, 200, 240) : IM_COL32(80, 200, 255, 240);
      const int kPlotPoints = 128;
      ImVec2 pts[kPlotPoints];

      for (int i = 0; i < kPlotPoints; i++)
      {
         const float t = (float)i / (float)(kPlotPoints - 1);
         const float phase = t * (float)(2.0 * M_PI);
         const float s = sinf(phase);
         float y = 0.0f;

         switch (waveform)
         {
            case 0: y = s; break;
            case 1: y = (s >= 0.0f ? 1.0f : -1.0f); break;
            case 2:
            default:
               y = (2.0f / (float)M_PI) * asinf(std::clamp(s, -1.0f, 1.0f));
               break;
         }

         pts[i] = ImVec2(origin.x + t * w, midY - y * (h * 0.42f));
      }

      for (int i = 0; i < kPlotPoints - 1; i++)
         dl->AddLine(pts[i], pts[i + 1], waveCol, 2.0f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         static const char* kWaves[] = { "sine", "square", "triangle" };
         char buf[64];
         snprintf(buf, sizeof(buf), "wave: %s - thresh: %.0fdB", kWaves[waveform], thresholdDb);
         SetAudioReadout("cycle shaper", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawCycleShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      const int waveform = std::clamp((int)std::round(n->Param("waveform")), 0, 2);
      const float thresholdDb = n->Param("threshold");
      const int smooth = std::clamp((int)std::round(n->Param("smooth")), 0, 32);
      const bool analog = n->Param("analog") != 0.0f;

      static const char* kWaves[] = { "sine", "square", "triangle" };
      char stat[80];
      snprintf(stat, sizeof(stat), "%s - %.0fdB - %dsmp%s", kWaves[waveform], thresholdDb, smooth, analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawCycleShaperVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4);
         static const std::vector<std::string> kWaveNames = { "sine", "square", "triangle" };
         row.Dropdown("shape", kWaveNames, waveform, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("waveform") = (float)i;
         });
         row.Knob("thresh", n->ParamPtr("threshold"), -60.0f, -12.0f, "%.0fdB", kKnobLarge);
         row.Knob("smooth", n->ParamPtr("smooth"), 0.0f, 32.0f, "%.0fsmp", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         bool analogBool = analog;
         if (AudioToggleButton("analog##cycleShaperAnalog", &analogBool, 64.0f))
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
      }

      EndAudioBody();
   }

   // ---- Spec Blur ------------------------------------------------------
   struct SpecBlurSpectrumState
   {
      std::vector<float> window;
      float smoothed[512] = {};
      SpecBlurSpectrumState() { window.assign(1024, 0.0f); }
   };
   static std::unordered_map<AudioEffectNode*, SpecBlurSpectrumState> sSpecBlurSpectrums;

   void DrawSpecBlurVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      // Grid lines: 100Hz, 1kHz, 10kHz
      const double sr = AudioEngine::Instance().SampleRate() > 0.0 ? AudioEngine::Instance().SampleRate() : 48000.0;
      auto hzToX = [&](float hz) -> float {
         const float norm = (log10f(std::max(hz, 20.0f)) - log10f(20.0f)) / (log10f(20000.0f) - log10f(20.0f));
         return origin.x + std::clamp(norm, 0.0f, 1.0f) * w;
      };

      dl->AddLine(ImVec2(hzToX(100.0f), origin.y), ImVec2(hzToX(100.0f), br.y), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(hzToX(1000.0f), origin.y), ImVec2(hzToX(1000.0f), br.y), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(hzToX(10000.0f), origin.y), ImVec2(hzToX(10000.0f), br.y), ScopeGridCol(), 1.0f);

      SpecBlurSpectrumState& st = sSpecBlurSpectrums[n];
      const int winSize = 1024;
      float tempBuf[1024];
      const int readCount = n->ReadSpectrumSamples(tempBuf, winSize);
      if (readCount > 0)
      {
         if (readCount >= winSize)
         {
            st.window.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
         }
         else
         {
            const int keep = winSize - readCount;
            std::copy(st.window.begin() + readCount, st.window.end(), st.window.begin());
            std::copy(tempBuf, tempBuf + readCount, st.window.begin() + keep);
         }
      }

      float re[1024];
      float im[1024];
      for (int i = 0; i < winSize; i++)
      {
         const float wnd = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(winSize - 1)));
         re[i] = st.window[i] * wnd;
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

      for (int i = 1; i < 512; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / (float)winSize);
         st.smoothed[i] = st.smoothed[i] * 0.8f + mag * 0.2f;
      }

      const int kPlotPoints = 128;
      ImVec2 pts[kPlotPoints];
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = 20.0f * powf(1000.0f, frac);
         const int bin = std::clamp((int)(hz * (float)winSize / (float)sr), 1, 511);
         const float mag = st.smoothed[bin];
         const float db = 20.0f * log10f(std::max(mag, 1e-4f));
         // Map -60 dB .. 0 dB to height
         const float normY = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * 0.9f) - 2.0f);
      }

      const ImU32 fillCol = isLight ? IM_COL32(140, 70, 220, 45) : IM_COL32(180, 100, 255, 45);
      const ImU32 lineCol = isLight ? IM_COL32(150, 60, 230, 240) : IM_COL32(200, 130, 255, 240);

      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = {
            pts[i],
            pts[i + 1],
            ImVec2(pts[i + 1].x, br.y),
            ImVec2(pts[i].x, br.y)
         };
         dl->AddConvexPolyFilled(quad, 4, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, 2.0f);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      const float blurTime = n->Param("blurTime");
      const float tilt = n->Param("tilt");
      const bool freeze = n->Param("freeze") > 0.5f;

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "blur: %.0fms - tilt: %+.2f%s", blurTime, tilt, freeze ? " (frozen)" : "");
         SetAudioReadout("spec blur", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawSpecBlurBody(GraphNode& gn, AudioEffectNode* n)
   {
      const float blurTime = n->Param("blurTime");
      const float tilt = n->Param("tilt");
      const float diffusion = n->Param("diffusion");
      const bool freeze = n->Param("freeze") > 0.5f;
      const bool analog = n->Param("analog") > 0.5f;

      char stat[80];
      snprintf(stat, sizeof(stat), "%.0fms - diff: %.2f%s%s", blurTime, diffusion, freeze ? " - frozen" : "", analog ? " - analog" : "");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawSpecBlurVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4, kKnobLarge);
         row.Knob("blur", n->ParamPtr("blurTime"), 10.0f, 5000.0f, "%.0fms", kKnobLarge);
         row.Knob("tilt", n->ParamPtr("tilt"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("diffuse", n->ParamPtr("diffusion"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      {
         bool freezeBool = freeze;
         if (AudioToggleButton("freeze##specBlurFreeze", &freezeBool, 64.0f))
            *n->ParamPtr("freeze") = freezeBool ? 1.0f : 0.0f;
         ImGui::SameLine();
         bool analogBool = analog;
         if (AudioToggleButton("analog##specBlurAnalog", &analogBool, 64.0f))
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
      }

      EndAudioBody();
   }

   // ---- Key-Snap / Spectrum Slide (Turbo 0.57, ported from upstream) ------
   // Same FFT window helper as Spec Blur's visualizer.
   static void UpdateSpectrumState(AudioEffectNode* n, SpecBlurSpectrumState& st)
   {
      const int winSize = 1024;
      float tempBuf[1024];
      const int readCount = n->ReadSpectrumSamples(tempBuf, winSize);
      if (readCount > 0)
      {
         if (readCount >= winSize)
            st.window.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
         else
         {
            std::copy(st.window.begin() + readCount, st.window.end(), st.window.begin());
            std::copy(tempBuf, tempBuf + readCount, st.window.begin() + (winSize - readCount));
         }
      }
      float re[1024];
      float im[1024];
      for (int i = 0; i < winSize; i++)
      {
         re[i] = st.window[i] * 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(winSize - 1)));
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);
      for (int i = 1; i < 512; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / (float)winSize);
         st.smoothed[i] = st.smoothed[i] * 0.7f + mag * 0.3f;
      }
   }

   static void DrawSpectrumCurve(ImDrawList* dl, const SpecBlurSpectrumState& st, ImVec2 origin, ImVec2 br,
                                 float loHz, float hiHz, float bottomPad, bool isLight)
   {
      const float w = br.x - origin.x, h = br.y - origin.y;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      const double sr = AudioEngine::Instance().SampleRate() > 0.0 ? AudioEngine::Instance().SampleRate() : 48000.0;
      const int kPlotPoints = 160;
      ImVec2 pts[kPlotPoints];
      for (int i = 0; i < kPlotPoints; i++)
      {
         const float frac = (float)i / (float)(kPlotPoints - 1);
         const float hz = powf(10.0f, logLo + frac * logSpan);
         const int bin = std::clamp((int)(hz * 1024.0f / (float)sr), 1, 511);
         const float db = 20.0f * log10f(std::max(st.smoothed[bin], 1e-4f));
         const float normY = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
         pts[i] = ImVec2(origin.x + frac * w, br.y - normY * (h * 0.9f) - bottomPad);
      }
      const ImU32 fillCol = isLight ? IM_COL32(140, 70, 220, 45) : IM_COL32(180, 100, 255, 45);
      const ImU32 lineCol = isLight ? IM_COL32(150, 60, 230, 240) : IM_COL32(200, 130, 255, 240);
      for (int i = 0; i < kPlotPoints - 1; i++)
      {
         ImVec2 quad[4] = { pts[i], pts[i + 1], ImVec2(pts[i + 1].x, br.y), ImVec2(pts[i].x, br.y) };
         dl->AddConvexPolyFilled(quad, 4, fillCol);
         dl->AddLine(pts[i], pts[i + 1], lineCol, 2.0f);
      }
   }

   static std::unordered_map<AudioEffectNode*, SpecBlurSpectrumState> sKeySnapSpectrums;
   static std::unordered_map<AudioEffectNode*, SpecBlurSpectrumState> sSpectrumSlideSpectrums;

   // The scale in use: the node's own, or the transport's when globalKey is on.
   void KeySnapScaleRoot(AudioEffectNode* n, int& scale, int& root)
   {
      scale = std::clamp((int)lroundf(n->Param("scale")), 0, (int)MusicTime::kNumScaleTypes - 1);
      root = std::clamp((int)lroundf(n->Param("root")), 0, 11);
      if (n->Param("globalKey") > 0.5f)
      {
         scale = std::clamp(Transport::Instance().Scale(), 0, (int)MusicTime::kNumScaleTypes - 1);
         root = ((Transport::Instance().Key() % 12) + 12) % 12;
      }
   }

   // Live output spectrum on a log axis with a line at every note of the
   // scale (root brighter): the peaks should sit on the lines.
   void DrawKeySnapVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float loHz = 50.0f, hiHz = 5000.0f;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      auto hzToX = [&](float hz) { return origin.x + std::clamp((log10f(hz) - logLo) / logSpan, 0.0f, 1.0f) * w; };
      int scale = 0, root = 0;
      KeySnapScaleRoot(n, scale, root);
      const uint32_t mask = KeySnapKernel::ScaleMask(scale, root);
      const ImU32 offCol = isLight ? IM_COL32(40, 40, 60, 14) : IM_COL32(255, 255, 255, 10);
      const ImU32 noteCol = isLight ? IM_COL32(40, 40, 60, 60) : IM_COL32(255, 255, 255, 48);
      const ImU32 rootCol = isLight ? IM_COL32(40, 40, 60, 130) : IM_COL32(255, 255, 255, 110);
      for (int pass = 0; pass < 3; pass++)
      {
         for (int midi = 24; midi <= 120; midi++)
         {
            const int pc = midi % 12;
            const int tier = pc == root ? 2 : ((mask & (1u << pc)) ? 1 : 0);
            if (tier != pass)
               continue;
            const float hz = 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f);
            if (hz < loHz || hz > hiHz)
               continue;
            const float x = hzToX(hz);
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), tier == 2 ? rootCol : (tier == 1 ? noteCol : offCol),
                        tier == 2 ? 1.5f : 1.0f);
         }
      }
      SpecBlurSpectrumState& st = sKeySnapSpectrums[n];
      UpdateSpectrumState(n, st);
      DrawSpectrumCurve(dl, st, origin, br, loHz, hiHz, 2.0f, isLight);
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);
      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%s %s", NoteNameList()[root].c_str(), MusicTime::ScaleTable(scale).name);
         SetAudioReadout("key-snap", buf);
      }
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawKeySnapBody(GraphNode& gn, AudioEffectNode* n)
   {
      int scale = 0, root = 0;
      KeySnapScaleRoot(n, scale, root);
      const bool globalKey = n->Param("globalKey") > 0.5f;
      char stat[80];
      snprintf(stat, sizeof(stat), "%s %s - snap %.0f%%%s", NoteNameList()[root].c_str(),
               MusicTime::ScaleTable(scale).name, n->Param("snap") * 100.0f, globalKey ? " - global key" : "");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawKeySnapVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(3, kKnobLarge);
         row.Knob("snap", n->ParamPtr("snap"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("glide", n->ParamPtr("glide"), 0.0f, 500.0f, "%.0fms", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3, 20.0f, 0.0f, false);
         if (globalKey)
            ImGui::BeginDisabled();
         row.Dropdown("scale", MusicTime::ScaleTypeList(), (int)lroundf(n->Param("scale")),
                      [n](int i) { PushUndoCheckpoint(); *n->ParamPtr("scale") = (float)i; });
         row.Dropdown("root", NoteNameList(), (int)lroundf(n->Param("root")),
                      [n](int i) { PushUndoCheckpoint(); *n->ParamPtr("root") = (float)i; });
         if (globalKey)
            ImGui::EndDisabled();
         bool globalBool = globalKey;
         if (row.Checkbox("global key##keySnapGlobal", &globalBool))
            *n->ParamPtr("globalKey") = globalBool ? 1.0f : 0.0f;
         row.End();
      }
      EndAudioBody();
   }

   // Live output spectrum on a log axis; a marker under it shows where the
   // slide sits between the input (left end) and 'to' (right end).
   void DrawSpectrumSlideVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);
      const float loHz = 50.0f, hiHz = 12000.0f;
      const float logLo = log10f(loHz), logSpan = log10f(hiHz) - logLo;
      for (float hz : { 100.0f, 1000.0f, 10000.0f })
      {
         const float x = origin.x + (log10f(hz) - logLo) / logSpan * w;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }
      SpecBlurSpectrumState& st = sSpectrumSlideSpectrums[n];
      UpdateSpectrumState(n, st);
      DrawSpectrumCurve(dl, st, origin, br, loHz, hiHz, 6.0f, isLight);
      const float slide = std::clamp(n->Param("slide"), 0.0f, 1.0f);
      const float ty = br.y - 3.0f;
      const ImU32 trackCol = isLight ? IM_COL32(40, 40, 60, 60) : IM_COL32(255, 255, 255, 50);
      const ImU32 dotCol = isLight ? IM_COL32(40, 40, 60, 200) : IM_COL32(255, 255, 255, 190);
      dl->AddLine(ImVec2(origin.x + 8.0f, ty), ImVec2(br.x - 8.0f, ty), trackCol, 1.0f);
      dl->AddCircleFilled(ImVec2(origin.x + 8.0f + slide * (w - 16.0f), ty), 2.5f, dotCol);
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);
      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.0f%% of the way from input to 'to'", slide * 100.0f);
         SetAudioReadout("spectrum slide", buf);
      }
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawSpectrumSlideBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "slide %.0f%% - in > to", n->Param("slide") * 100.0f);
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawSpectrumSlideVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(2, kKnobLarge);
         row.Knob("slide", n->ParamPtr("slide"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioBody();
   }

   // ---- Audio Filter -----------------------------------------------------
   // Cached per-node response curve: the underlying MagnitudeDb sweep is a
   // settled-sine measurement (AudioFilterKernel.h), cheap per call but not
   // free across ~150 points x up to 4 bands every single ImGui frame, so it
   // is only recomputed when something that would actually change the curve
   // has (per audio-node-ui-system.md §3f's "computed main-thread ... never
   // by calling into the live AudioNode" - this recomputes from a *scratch*
   // Biquad/TptSvf, same as the kernel's own PushParams, not the running one).
   struct FilterCurveCache
   {
      std::vector<float> curveDb; // one entry per x pixel column sampled
      std::vector<float> signature;
      bool dragIsQ = false; // which handle the current drag (if any) is grabbing
   };
   std::map<int, FilterCurveCache> gFilterCurveCache;

   const float kFilterVizMinHz = 20.0f;
   const float kFilterVizMaxHz = 20000.0f;
   const float kFilterVizMinDb = -24.0f;
   const float kFilterVizMaxDb = 24.0f;

   float FilterVizFreqToX(float hz, float x0, float w)
   {
      const float t = (logf(std::max(1.0f, hz)) - logf(kFilterVizMinHz)) /
                      (logf(kFilterVizMaxHz) - logf(kFilterVizMinHz));
      return x0 + std::clamp(t, 0.0f, 1.0f) * w;
   }

   float FilterVizXToFreq(float x, float x0, float w)
   {
      const float t = std::clamp((x - x0) / std::max(1.0f, w), 0.0f, 1.0f);
      return kFilterVizMinHz * powf(kFilterVizMaxHz / kFilterVizMinHz, t);
   }

   float FilterVizDbToY(float db, float y0, float h)
   {
      const float t = (db - kFilterVizMinDb) / (kFilterVizMaxDb - kFilterVizMinDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }

   float FilterVizYToDb(float y, float y0, float h)
   {
      const float t = std::clamp((y0 + h - y) / h, 0.0f, 1.0f);
      return kFilterVizMinDb + t * (kFilterVizMaxDb - kFilterVizMinDb);
   }

   // Full-width log-frequency response curve with a draggable handle per
   // band - the reason Audio Filter is built first (§1.1): X = freq,
   // Y = gain, scroll = Q, double-click = enable/disable. This *is* the
   // Tier 1 control surface's picture, per audio-node-ui-system.md §3g.
   // Live incoming-signal spectrum drawn behind the response curve (the
   // thing FabFilter Pro-Q shows) - shared by Audio Filter and EQ so both
   // visualizers show the same live analyzer, not two separate ones. Taps
   // the post-mix mono ring AudioEffectRuntime writes every audio block
   // (AudioEffectNode.cpp's mSpectrumRing) and runs it through the same
   // Hann-window + Radix2FFT recipe AudioColorRampNode::ProcessAudioFFT
   // already uses (AudioColorRampNode.cpp) - just plotted against this
   // visualizer's own log-frequency axis instead of feeding a color ramp.
   struct AudioSpectrumState
   {
      std::vector<float> window = std::vector<float>(1024, 0.0f);
      std::vector<float> smoothed = std::vector<float>(512, 0.0f);
   };
   std::map<int, AudioSpectrumState> gAudioSpectrumCache;

   // Spectrum level has its own dB scale, independent of the +-24 dB gain
   // axis the response curve uses - same relationship as Pro-Q, where the
   // analyzer trace and the response curve share an x-axis but not a y-axis.
   const float kAudioSpectrumFloorDb = -72.0f;
   const float kAudioSpectrumCeilDb = 0.0f;

   float AudioSpectrumDbToY(float db, float y0, float h)
   {
      const float t = (db - kAudioSpectrumFloorDb) / (kAudioSpectrumCeilDb - kAudioSpectrumFloorDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }

   void UpdateAudioSpectrum(AudioEffectNode* n, AudioSpectrumState& st)
   {
      const int winSize = 1024;
      float tempBuf[1024];
      const int readCount = n->ReadSpectrumSamples(tempBuf, winSize);
      if (readCount > 0)
      {
         if (readCount >= winSize)
         {
            st.window.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
         }
         else
         {
            const int keep = winSize - readCount;
            std::copy(st.window.begin() + readCount, st.window.end(), st.window.begin());
            std::copy(tempBuf, tempBuf + readCount, st.window.begin() + keep);
         }
      }
      // Hann window, built once with the exact expression it used to be
      // evaluated with per sample, per node, per frame (1024 cosf each).
      static const std::vector<float> sHann = [] {
         std::vector<float> w(1024);
         for (int i = 0; i < 1024; i++)
            w[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * (float)i / (float)(1024 - 1)));
         return w;
      }();
      float re[1024];
      float im[1024];
      for (int i = 0; i < winSize; i++)
      {
         re[i] = st.window[i] * sHann[i];
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

      for (int i = 1; i < 512; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / (float)winSize);
         st.smoothed[i] = st.smoothed[i] * 0.7f + mag * 0.3f;
      }
   }

   void DrawAudioSpectrum(ImDrawList* dl, ImVec2 origin, float w, float h, double sampleRate, const AudioSpectrumState& st,
                       bool isLight)
   {
      const float binWidth = (float)(sampleRate > 0.0 ? sampleRate : 44100.0) / 1024.0f;
      const float baselineY = origin.y + h;
      const ImU32 fillCol = isLight ? IM_COL32(70, 90, 120, 40) : IM_COL32(210, 220, 235, 40);
      const ImU32 lineCol = isLight ? IM_COL32(70, 90, 120, 130) : IM_COL32(210, 220, 235, 130);

      float prevX = FilterVizFreqToX(binWidth, origin.x, w);
      float prevY = AudioSpectrumDbToY(20.0f * log10f(std::max(st.smoothed[1], 1.0e-6f)), origin.y, h);
      dl->PathClear();
      dl->PathLineTo(ImVec2(prevX, prevY));
      for (int i = 2; i < 512; i++)
      {
         const float freq = (float)i * binWidth;
         const float db = 20.0f * log10f(std::max(st.smoothed[i], 1.0e-6f));
         const float x = FilterVizFreqToX(freq, origin.x, w);
         const float y = AudioSpectrumDbToY(db, origin.y, h);
         dl->AddQuadFilled(ImVec2(prevX, baselineY), ImVec2(x, baselineY), ImVec2(x, y), ImVec2(prevX, prevY), fillCol);
         dl->PathLineTo(ImVec2(x, y));
         prevX = x;
         prevY = y;
      }
      dl->PathStroke(lineCol, 0, 1.2f);
   }

   // Full-resolution response curve, exactly as the visualizer computes it -
   // the one definition both the cache and FILTERCURVECACHETEST use.
   void ComputeFilterCurve(std::vector<float>& out, int numPoints, int type, float freq, float q, float gain,
                           double sampleRate, float originX, float w)
   {
      out.resize(numPoints);
      for (int i = 0; i < numPoints; i++)
      {
         const float x = originX + (float)i * (w / (float)(numPoints - 1));
         const float f = FilterVizXToFreq(x, originX, w);
         out[i] = AudioFilterDsp::MagnitudeDb(type, freq, q, gain, f, sampleRate);
      }
   }

   void DrawAudioFilterVisualizer(AudioEffectNode* n, double sampleRate)
   {
      const float w = gAudioBodyW;
      const float h = 190.0f; // fills the space the old per-band strip used to occupy
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      ImGui::InvisibleButton("##filterCurve", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      // Octave-ish frequency ticks and dB ticks - the graticule that keeps
      // this from reading as blank at rest (§3f).
      static const float kFreqTicks[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                          2000.0f, 5000.0f, 10000.0f };
      for (float f : kFreqTicks)
      {
         const float x = FilterVizFreqToX(f, origin.x, w);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }
      static const float kDbTicks[] = { -12.0f, 0.0f, 12.0f };
      for (float db : kDbTicks)
      {
         const float y = FilterVizDbToY(db, origin.y, h);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y),
                     db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
      }
      // live incoming spectrum behind the curve (own dB axis, like Pro-Q)
      {
         AudioSpectrumState& specState = gAudioSpectrumCache[gCurrentNodeIndex];
         UpdateAudioSpectrum(n, specState);
         DrawAudioSpectrum(dl, origin, w, h, sampleRate, specState, isLight);
      }

      const float type = n->Param("type");
      const float freq = n->Param("freq");
      const float q = n->Param("q");
      const float gain = n->Param("gain");

      // Recompute the curve only if the signature (everything it depends on)
      // actually changed since last frame.
      std::vector<float> sig{ (float)sampleRate, type, freq, q, gain };
      FilterCurveCache& cache = gFilterCurveCache[gCurrentNodeIndex];
      const int kNumPoints = 160;
      if (cache.signature != sig)
      {
         cache.signature = sig;
         cache.curveDb.resize(kNumPoints);
         for (int i = 0; i < kNumPoints; i++)
         {
            const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
            const float f = FilterVizXToFreq(x, origin.x, w);
            cache.curveDb[i] = AudioFilterDsp::MagnitudeDb((int)(type + 0.5f), freq, q, gain, f, sampleRate);
         }
      }

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
      {
         const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
         const float y = FilterVizDbToY(cache.curveDb[i], origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      // Two handles sharing the single ##filterCurve button above - the
      // round one sets freq/gain, the diamond sets Q. Q used to ride the
      // scroll wheel, but that gesture is also how the canvas itself zooms
      // - scrolling to zoom while the cursor happened to be over this
      // viewport silently dragged Q along with it, with no relation to the
      // real Q knob. A second real ImGui item drawn on top of the first
      // (to make Q separately clickable) turned out to disturb the layout
      // cursor for the knob row drawn after this function, breaking drag on
      // both handles - so instead this stays one button, and on click we
      // just decide (by proximity) which handle that particular drag grabs,
      // remembering the choice for the rest of the drag.
      const float hx = FilterVizFreqToX(freq, origin.x, w);
      const float hy = FilterVizDbToY(AudioFilterDsp::UsesGain((int)(type + 0.5f)) ? gain : 0.0f, origin.y, h);

      const float qT = (logf(std::max(0.1f, q)) - logf(0.1f)) / (logf(18.0f) - logf(0.1f));
      const float qx = std::clamp(hx + 16.0f, origin.x + 8.0f, br.x - 8.0f);
      const float qy = origin.y + h - std::clamp(qT, 0.0f, 1.0f) * h;

      if (ImGui::IsItemActivated())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const float dMain = (m.x - hx) * (m.x - hx) + (m.y - hy) * (m.y - hy);
         const float dQ = (m.x - qx) * (m.x - qx) + (m.y - qy) * (m.y - qy);
         cache.dragIsQ = dQ < dMain;
      }

      const bool isNear = hovered || active;

      if (active && cache.dragIsQ)
      {
         const float my = ImGui::GetIO().MousePos.y;
         const float t = std::clamp((origin.y + h - my) / h, 0.0f, 1.0f);
         *n->ParamPtr("q") = std::clamp(0.1f * powf(18.0f / 0.1f, t), 0.1f, 18.0f);
      }
      else if (active)
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         *n->ParamPtr("freq") = std::clamp(FilterVizXToFreq(m.x, origin.x, w), kFilterVizMinHz, kFilterVizMaxHz);
         if (AudioFilterDsp::UsesGain((int)(type + 0.5f)))
            *n->ParamPtr("gain") = FilterVizYToDb(m.y, origin.y, h);
      }

      const bool mainNear = isNear && !(active && cache.dragIsQ);
      dl->AddCircleFilled(ImVec2(hx, hy), mainNear ? 5.0f : 3.6f, IM_COL32(235, 245, 255, 255), 12);
      dl->AddCircle(ImVec2(hx, hy), mainNear ? 5.0f : 3.6f, IM_COL32(20, 24, 32, 220), 12, 1.5f);

      // Diamond handle for Q, offset beside the main dot so both stay
      // visually distinct.
      const bool qNear = isNear && (!active || cache.dragIsQ);
      const float qr = qNear ? 5.5f : 4.0f;
      const ImVec2 qPts[4] = { ImVec2(qx, qy - qr), ImVec2(qx + qr, qy), ImVec2(qx, qy + qr), ImVec2(qx - qr, qy) };
      dl->AddConvexPolyFilled(qPts, 4, IM_COL32(255, 205, 120, 255));
      dl->AddPolyline(qPts, 4, IM_COL32(20, 24, 32, 220), ImDrawFlags_Closed, 1.5f);

      dl->PopClipRect();
      dl->AddRect(origin, br, hovered ? (isLight ? IM_COL32(50, 120, 220, 255) : IM_COL32(110, 140, 180, 255))
                                      : ScopeBorderCol(), 3.0f);

      if (isNear)
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.0f Hz  %+.1f dB  Q %.2f", freq, gain, q);
         SetAudioReadout("filter", buf);
      }
      // No trailing Dummy here: the InvisibleButton above already reserved
      // this (w, h) layout space - a second Dummy of the same size would
      // double-reserve it and leave a blank gap the height of the graph
      // between it and whatever's drawn next.
   }

   // Turbo 0.51: "analog" character-mode checkbox (appended param on the
   // effects ported from upstream), drawn after the effect's other controls.
   void DrawAnalogToggle(AudioEffectNode* n, const char* checkboxId)
   {
      bool analog = n->Param("analog") != 0.0f;
      if (ModCheckbox(checkboxId, &analog))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("analog") = analog ? 1.0f : 0.0f;
      }
   }

   void AddSyncedRateCell(AudioKnobRow& row, AudioEffectNode* n);
   void DrawSyncToggle(AudioEffectNode* n, const char* checkboxId);

   void DrawAudioFilterBody(GraphNode& gn, AudioEffectNode* n)
   {
      const int type = (int)(n->Param("type") + 0.5f);
      const float freq = n->Param("freq");
      const float q = n->Param("q");

      char stat[80];
      snprintf(stat, sizeof(stat), "%s - %.1f kHz - Q %.2f", AudioFilterDsp::TypeName(type),
               freq / 1000.0f, q);

      const double sampleRate = AudioEngine::Instance().SampleRate() > 0.0
                                   ? AudioEngine::Instance().SampleRate()
                                   : 44100.0;

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawAudioFilterVisualizer(n, sampleRate);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(4);
         row.Dropdown("type", AudioFilterDsp::TypeList(), type, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("type") = (float)i;
         });
         row.Knob("freq", n->ParamPtr("freq"), 20.0f, 20000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("Q", n->ParamPtr("q"), 0.1f, 18.0f, "%.2f", kKnobLarge);

         const bool gainLive = AudioFilterDsp::UsesGain(type);
         if (!gainLive)
            ImGui::BeginDisabled();
         row.Knob("gain", n->ParamPtr("gain"), -24.0f, 24.0f, "%.1f dB", kKnobLarge);
         if (!gainLive)
            ImGui::EndDisabled();
         row.End();
      }

      BeginAudioSection("output");
      {
         AudioKnobRow row(4);
         row.Knob("output gain", n->ParamPtr("outputGainDb"), -24.0f, 12.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("env", n->ParamPtr("envAmount"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mod", n->ParamPtr("modAmount"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioSection();

      {
         // Turbo 0.51: internal sine LFO sweep (depth, rate, sync).
         AudioKnobRow row(2);
         row.Knob("lfo", n->ParamPtr("lfoAmount"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         AddSyncedRateCell(row, n);
         row.End();
      }
      DrawSyncToggle(n, "lfo sync to tempo##afLfoSync");

      EndAudioBody();
   }

   // ---- EQ -------------------------------------------------------------
   // Five fixed bands, always present - docs/plans/audio/eq-node-prompt.md.
   // Reuses Audio Filter's log-frequency graticule/mapping helpers verbatim
   // (FilterVizFreqToX/XToFreq/DbToY/YToDb, kFilterViz*) rather than defining
   // a second copy - both nodes share the same 20 Hz-20 kHz / +-24 dB frame.
   struct EqCurveCache
   {
      std::vector<float> curveDb;       // composite, one entry per x column
      std::vector<float> bandCurveDb[5]; // per-band, only enabled ones drawn
      std::vector<float> signature;
      int dragBand = -1;
      bool dragIsQ = false;
      bool dragInert = false; // this gesture is a select-only click or a double-click toggle
   };
   std::map<int, EqCurveCache> gEqCurveCache;

   static const char* const kEqTypeParam[5] = { "band1Type", "band2Type", "band3Type", "band4Type", "band5Type" };
   static const char* const kEqFreqParam[5] = { "band1Freq", "band2Freq", "band3Freq", "band4Freq", "band5Freq" };
   static const char* const kEqQParam[5] = { "band1Q", "band2Q", "band3Q", "band4Q", "band5Q" };
   static const char* const kEqGainParam[5] = { "band1Gain", "band2Gain", "band3Gain", "band4Gain", "band5Gain" };
   static const char* const kEqOnParam[5] = { "band1On", "band2On", "band3On", "band4On", "band5On" };

   struct EqBandValues
   {
      int type;
      float freq, q, gain;
      bool on;
   };

   void ReadEqBands(AudioEffectNode* n, EqBandValues bands[5])
   {
      for (int b = 0; b < 5; b++)
      {
         bands[b].type = std::clamp((int)(n->Param(kEqTypeParam[b]) + 0.5f), 0, EqDsp::kNumBandTypes - 1);
         bands[b].freq = n->Param(kEqFreqParam[b]);
         bands[b].q = n->Param(kEqQParam[b]);
         bands[b].gain = n->Param(kEqGainParam[b]);
         bands[b].on = n->Param(kEqOnParam[b]) >= 0.5f;
      }
   }

   void DrawEqVisualizer(AudioEffectNode* n, double sampleRate)
   {
      const float w = gAudioBodyW;
      const float h = 190.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      gEqTestRect = ImVec4(origin.x, origin.y, br.x, br.y);

      ImGui::InvisibleButton("##eqCurve", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      static const float kFreqTicks[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                          2000.0f, 5000.0f, 10000.0f };
      for (float f : kFreqTicks)
      {
         const float x = FilterVizFreqToX(f, origin.x, w);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }
      static const float kDbTicks[] = { -12.0f, 0.0f, 12.0f };
      for (float db : kDbTicks)
      {
         const float y = FilterVizDbToY(db, origin.y, h);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y),
                     db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
      }
      // live incoming spectrum behind the curve (own dB axis, like Pro-Q)
      {
         AudioSpectrumState& specState = gAudioSpectrumCache[gCurrentNodeIndex];
         UpdateAudioSpectrum(n, specState);
         DrawAudioSpectrum(dl, origin, w, h, sampleRate, specState, isLight);
      }

      EqBandValues bands[5];
      ReadEqBands(n, bands);
      int selected = std::clamp((int)(n->Param("selectedBand") + 0.5f), 0, 4);

      std::vector<float> sig;
      sig.reserve(26);
      sig.push_back((float)sampleRate);
      for (int b = 0; b < 5; b++)
      {
         sig.push_back((float)bands[b].type);
         sig.push_back(bands[b].freq);
         sig.push_back(bands[b].q);
         sig.push_back(bands[b].gain);
         sig.push_back(bands[b].on ? 1.0f : 0.0f);
      }

      EqCurveCache& cache = gEqCurveCache[gCurrentNodeIndex];
      const int kNumPoints = 160;
      if (cache.signature != sig)
      {
         cache.signature = sig;
         cache.curveDb.resize(kNumPoints);
         for (int b = 0; b < 5; b++)
            cache.bandCurveDb[b].resize(kNumPoints);
         for (int i = 0; i < kNumPoints; i++)
         {
            const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
            const float f = FilterVizXToFreq(x, origin.x, w);
            float composite = 0.0f;
            for (int b = 0; b < 5; b++)
            {
               const float bandDb =
                  EqDsp::BandMagnitudeDb(bands[b].type, bands[b].freq, bands[b].q, bands[b].gain,
                                          bands[b].on, f, sampleRate);
               cache.bandCurveDb[b][i] = bandDb;
               composite += bandDb;
            }
            cache.curveDb[i] = composite;
         }
      }

      // Per-band curves at low alpha, only for enabled bands - a disabled
      // band draws a hollow dot and no curve (§3f, "never blank" cuts both
      // ways: absence is shown, not hidden).
      for (int b = 0; b < 5; b++)
      {
         if (!bands[b].on)
            continue;
         dl->PathClear();
         for (int i = 0; i < kNumPoints; i++)
         {
            const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
            const float y = FilterVizDbToY(cache.bandCurveDb[b][i], origin.y, h);
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(IM_COL32(150, 214, 255, 60), 0, 1.0f);
      }

      // Composite curve, bright, with a soft fill down to the 0 dB baseline -
      // built from per-segment quads rather than AddConvexPolyFilled, which
      // assumes convexity a response curve doesn't guarantee.
      const float baselineY = FilterVizDbToY(0.0f, origin.y, h);
      for (int i = 0; i + 1 < kNumPoints; i++)
      {
         const float x0 = origin.x + (float)i * (w / (float)(kNumPoints - 1));
         const float x1 = origin.x + (float)(i + 1) * (w / (float)(kNumPoints - 1));
         const float y0 = FilterVizDbToY(cache.curveDb[i], origin.y, h);
         const float y1 = FilterVizDbToY(cache.curveDb[i + 1], origin.y, h);
         dl->AddQuadFilled(ImVec2(x0, baselineY), ImVec2(x1, baselineY), ImVec2(x1, y1), ImVec2(x0, y0),
                           isLight ? IM_COL32(30, 110, 230, 30) : IM_COL32(150, 214, 255, 22));
      }
      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
      {
         const float x = origin.x + (float)i * (w / (float)(kNumPoints - 1));
         const float y = FilterVizDbToY(cache.curveDb[i], origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      // Handle positions for every band - one dot (freq/gain) and one
      // diamond (Q) each, sharing the single ##eqCurve button above for the
      // same reason Audio Filter's do (a second real ImGui item per handle
      // disturbs the layout cursor for the knob row drawn afterward).
      float hx[5], hy[5], qx[5], qy[5];
      for (int b = 0; b < 5; b++)
      {
         hx[b] = FilterVizFreqToX(bands[b].freq, origin.x, w);
         hy[b] = FilterVizDbToY(EqDsp::UsesGain(bands[b].type) ? bands[b].gain : 0.0f, origin.y, h);
         const float qT = (logf(std::max(0.1f, bands[b].q)) - logf(0.1f)) / (logf(18.0f) - logf(0.1f));
         qx[b] = std::clamp(hx[b] + 16.0f, origin.x + 8.0f, br.x - 8.0f);
         qy[b] = origin.y + h - std::clamp(qT, 0.0f, 1.0f) * h;
      }

      if (ImGui::IsItemActivated())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         float bestDist2 = 1.0e30f;
         int bestBand = selected;
         bool bestIsQ = false;
         float bestDotDist2 = 1.0e30f;
         int bestDotBand = selected;
         for (int b = 0; b < 5; b++)
         {
            const float dDot = (m.x - hx[b]) * (m.x - hx[b]) + (m.y - hy[b]) * (m.y - hy[b]);
            const float dQ = (m.x - qx[b]) * (m.x - qx[b]) + (m.y - qy[b]) * (m.y - qy[b]);
            if (dDot < bestDist2) { bestDist2 = dDot; bestBand = b; bestIsQ = false; }
            if (dQ < bestDist2) { bestDist2 = dQ; bestBand = b; bestIsQ = true; }
            if (dDot < bestDotDist2) { bestDotDist2 = dDot; bestDotBand = b; }
         }
         const float kHandleGrabRadius2 = 20.0f * 20.0f;
         cache.dragInert = false;
         if (bestDist2 <= kHandleGrabRadius2)
         {
            cache.dragBand = bestBand;
            cache.dragIsQ = bestIsQ;
         }
         else
         {
            // Click landed away from every handle - select the nearest band
            // by its dot, but this gesture does not move anything.
            cache.dragBand = bestDotBand;
            cache.dragIsQ = false;
            cache.dragInert = true;
         }
         *n->ParamPtr("selectedBand") = (float)cache.dragBand;
         selected = cache.dragBand;
      }

      // Double-click a band's dot toggles its bandOn, and cancels this
      // gesture's drag so the toggle doesn't also nudge freq/gain.
      if (hovered && ImGui::IsMouseDoubleClicked(0))
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         float bestDotDist2 = 1.0e30f;
         int bestDotBand = -1;
         for (int b = 0; b < 5; b++)
         {
            const float dDot = (m.x - hx[b]) * (m.x - hx[b]) + (m.y - hy[b]) * (m.y - hy[b]);
            if (dDot < bestDotDist2) { bestDotDist2 = dDot; bestDotBand = b; }
         }
         const float kToggleRadius2 = 20.0f * 20.0f;
         if (bestDotBand >= 0 && bestDotDist2 <= kToggleRadius2)
         {
            *n->ParamPtr(kEqOnParam[bestDotBand]) = bands[bestDotBand].on ? 0.0f : 1.0f;
            cache.dragInert = true;
         }
      }

      const int dragBand = std::clamp(cache.dragBand < 0 ? selected : cache.dragBand, 0, 4);
      if (active && !cache.dragInert)
      {
         if (cache.dragIsQ)
         {
            const float my = ImGui::GetIO().MousePos.y;
            const float t = std::clamp((origin.y + h - my) / h, 0.0f, 1.0f);
            *n->ParamPtr(kEqQParam[dragBand]) = std::clamp(0.1f * powf(18.0f / 0.1f, t), 0.1f, 18.0f);
         }
         else if (ImGui::GetIO().KeyShift)
         {
            // Turbo (as upstream): Shift-drag a band dot = its Q, freq/gain stay put.
            float* qp = n->ParamPtr(kEqQParam[dragBand]);
            *qp = std::clamp(*qp * expf(-ImGui::GetIO().MouseDelta.y * 0.012f), 0.1f, 18.0f);
         }
         else
         {
            const ImVec2 m = ImGui::GetIO().MousePos;
            *n->ParamPtr(kEqFreqParam[dragBand]) =
               std::clamp(FilterVizXToFreq(m.x, origin.x, w), kFilterVizMinHz, kFilterVizMaxHz);
            if (EqDsp::UsesGain(bands[dragBand].type))
               *n->ParamPtr(kEqGainParam[dragBand]) = std::clamp(FilterVizYToDb(m.y, origin.y, h), -24.0f, 24.0f);
         }
      }

      // Draw every non-selected band's handles first, then the selected
      // band's larger and last, so it is never occluded by a neighbour.
      for (int pass = 0; pass < 2; pass++)
      {
         for (int b = 0; b < 5; b++)
         {
            const bool isSelected = (b == selected);
            if ((pass == 0) == isSelected)
               continue;

            const bool isDragTarget = active && !cache.dragInert && dragBand == b;
            const bool qActiveHere = isDragTarget && cache.dragIsQ;
            const bool dotActiveHere = isDragTarget && !cache.dragIsQ;

            const float dotR = isSelected ? (dotActiveHere ? 6.5f : 4.8f) : (dotActiveHere ? 5.0f : 3.2f);
            if (bands[b].on)
            {
               dl->AddCircleFilled(ImVec2(hx[b], hy[b]), dotR, IM_COL32(235, 245, 255, isSelected ? 255 : 170), 12);
            }
            else
            {
               dl->AddCircle(ImVec2(hx[b], hy[b]), dotR, IM_COL32(235, 245, 255, isSelected ? 220 : 120), 12, 1.5f);
            }
            dl->AddCircle(ImVec2(hx[b], hy[b]), dotR, IM_COL32(20, 24, 32, 220), 12, 1.2f);

            const float qr = isSelected ? (qActiveHere ? 6.5f : 5.0f) : (qActiveHere ? 5.5f : 3.6f);
            const ImU32 qCol = isSelected ? IM_COL32(255, 205, 120, 255) : IM_COL32(255, 205, 120, 150);
            const ImVec2 qPts[4] = { ImVec2(qx[b], qy[b] - qr), ImVec2(qx[b] + qr, qy[b]),
                                     ImVec2(qx[b], qy[b] + qr), ImVec2(qx[b] - qr, qy[b]) };
            dl->AddConvexPolyFilled(qPts, 4, qCol);
            dl->AddPolyline(qPts, 4, IM_COL32(20, 24, 32, 220), ImDrawFlags_Closed, 1.2f);
         }
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, hovered ? (isLight ? IM_COL32(50, 120, 220, 255) : IM_COL32(110, 140, 180, 255))
                                      : ScopeBorderCol(), 3.0f);

      if (hovered || active)
      {
         const int b = dragBand;
         char buf[80];
         snprintf(buf, sizeof(buf), "band %d  %.0f Hz  %+.1f dB  Q %.2f  %s", b + 1, bands[b].freq,
                  bands[b].gain, bands[b].q, bands[b].on ? "on" : "off");
         SetAudioReadout("eq", buf);
      }
      // No trailing Dummy: the InvisibleButton above already reserved this
      // (w, h) layout space.
   }

   void DrawEqBody(GraphNode& gn, AudioEffectNode* n)
   {
      EqBandValues bands[5];
      ReadEqBands(n, bands);

      int active = 0;
      float peakDb = 0.0f;
      for (int b = 0; b < 5; b++)
         if (bands[b].on)
            active++;
      {
         // Coarse scan for the idle-stat peak, independent of the
         // visualizer's own (denser, cached) curve.
         const double sampleRate = AudioEngine::Instance().SampleRate() > 0.0
                                      ? AudioEngine::Instance().SampleRate()
                                      : 44100.0;
         const int kScanPoints = 48;
         float bestAbs = -1.0f;
         for (int i = 0; i < kScanPoints; i++)
         {
            const float t = (float)i / (float)(kScanPoints - 1);
            const float f = kFilterVizMinHz * powf(kFilterVizMaxHz / kFilterVizMinHz, t);
            float composite = 0.0f;
            for (int b = 0; b < 5; b++)
               composite += EqDsp::BandMagnitudeDb(bands[b].type, bands[b].freq, bands[b].q, bands[b].gain,
                                                    bands[b].on, f, sampleRate);
            if (std::fabs(composite) > bestAbs)
            {
               bestAbs = std::fabs(composite);
               peakDb = composite;
            }
         }
      }

      char stat[80];
      snprintf(stat, sizeof(stat), "5 bands - %d active - %+.1f dB peak", active, peakDb);

      const double sampleRate = AudioEngine::Instance().SampleRate() > 0.0
                                   ? AudioEngine::Instance().SampleRate()
                                   : 44100.0;

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawEqVisualizer(n, sampleRate);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      const int selBand = std::clamp((int)(n->Param("selectedBand") + 0.5f), 0, 4);
      const int selType = bands[selBand].type;

      {
         AudioKnobRow row(4);
         row.Dropdown("type", EqDsp::TypeList(), selType, [n, selBand](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr(kEqTypeParam[selBand]) = (float)i;
         });
         row.Knob("freq", n->ParamPtr(kEqFreqParam[selBand]), 20.0f, 20000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("Q", n->ParamPtr(kEqQParam[selBand]), 0.1f, 18.0f, "%.2f", kKnobLarge);

         const bool gainLive = EqDsp::UsesGain(selType);
         if (!gainLive)
            ImGui::BeginDisabled();
         row.Knob("gain", n->ParamPtr(kEqGainParam[selBand]), -24.0f, 24.0f, "%.1f dB", kKnobLarge);
         if (!gainLive)
            ImGui::EndDisabled();
         row.End();
      }

      BeginAudioSection("output");
      {
         AudioKnobRow row(2);
         row.Knob("output gain", n->ParamPtr("outputGainDb"), -24.0f, 12.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }

   // ---- Dynamics -----------------------------------------------------
   // Transfer curve (input dB -> output dB) with the live operating point
   // riding on it and a gain-reduction bar down the right edge - §1.2's
   // visualizer. The curve is main-thread from params (DynamicsDsp::
   // GainComputerDb, the same pure function the kernel's own per-sample gain
   // computer uses - never the live running kernel, per audio-node-ui-
   // system.md §3f); the dot and bar are the kernel's two published
   // ExtraMeterValue()s.
   const float kDynVizMinDb = -60.0f;
   const float kDynVizMaxDb = 6.0f;

   float DynVizXToDb(float x, float x0, float w)
   {
      const float t = std::clamp((x - x0) / std::max(1.0f, w), 0.0f, 1.0f);
      return kDynVizMinDb + t * (kDynVizMaxDb - kDynVizMinDb);
   }

   float DynVizDbToX(float db, float x0, float w)
   {
      const float t = (db - kDynVizMinDb) / (kDynVizMaxDb - kDynVizMinDb);
      return x0 + std::clamp(t, 0.0f, 1.0f) * w;
   }

   float DynVizDbToY(float db, float y0, float h)
   {
      const float t = (db - kDynVizMinDb) / (kDynVizMaxDb - kDynVizMinDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }

   void DrawDynamicsVisualizer(AudioEffectNode* n, float threshold, float ratio, float makeupDb)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const float grMeterW = 10.0f;
      const float grMeterGap = 6.0f;
      const float curveW = w - grMeterW - grMeterGap;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      const ImVec2 curveBr(origin.x + curveW, br.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, ImVec2(curveBr.x, br.y), ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, curveBr, true);

      static const float kDbTicks[] = { -48.0f, -36.0f, -24.0f, -12.0f, 0.0f };
      for (float db : kDbTicks)
      {
         const float x = DynVizDbToX(db, origin.x, curveW);
         const float y = DynVizDbToY(db, origin.y, h);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, curveBr.y), db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(curveBr.x, y),
                     db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         char tickBuf[8];
         snprintf(tickBuf, sizeof(tickBuf), "%.0f", db);
         dl->AddText(ImVec2(origin.x + 3.0f, y - (db == 0.0f ? 14.0f : 1.0f)), ScopeTextCol(), tickBuf);
      }

      // 1:1 reference diagonal (unprocessed signal), dim.
      dl->AddLine(ImVec2(DynVizDbToX(kDynVizMinDb, origin.x, curveW), DynVizDbToY(kDynVizMinDb, origin.y, h)),
                  ImVec2(DynVizDbToX(kDynVizMaxDb, origin.x, curveW), DynVizDbToY(kDynVizMaxDb, origin.y, h)),
                  IM_COL32(255, 255, 255, 24), 1.0f);

      // Threshold marker - a vertical line at the knee so the point the
      // curve bends at reads as a control, not just an inflection the eye
      // has to find. Amber to stay distinct from the curve (blue) and the
      // live operating point (white) drawn below.
      const ImU32 thresholdCol = isLight ? IM_COL32(210, 130, 20, 200) : IM_COL32(255, 180, 70, 190);
      const float threshX = DynVizDbToX(threshold, origin.x, curveW);
      dl->AddLine(ImVec2(threshX, origin.y), ImVec2(threshX, curveBr.y), thresholdCol, 1.5f);
      dl->AddText(ImVec2(threshX + 3.0f, origin.y + 2.0f), thresholdCol, "threshold");

      // The gain-computer curve itself, with makeup gain folded in so the
      // picture matches what actually comes out. The curve's bend at
      // threshold and its slope past it (1/ratio) ARE threshold and ratio -
      // this is the static shape of the compressor, independent of program
      // material.
      dl->PathClear();
      const int kNumPoints = 96;
      for (int i = 0; i < kNumPoints; i++)
      {
         const float x = origin.x + (float)i * (curveW / (float)(kNumPoints - 1));
         const float xDb = DynVizXToDb(x, origin.x, curveW);
         const float yDb = DynamicsDsp::GainComputerDb(xDb, threshold, ratio) + makeupDb;
         dl->PathLineTo(ImVec2(x, DynVizDbToY(yDb, origin.y, h)));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      // Live operating point - where the signal actually is right now, read
      // straight off the kernel's own smoothed envelope (ExtraMeterValue),
      // never recomputed from the static curve. That's what makes attack and
      // release visible: the dot lags behind a sudden input jump and eases
      // back down exactly on the kernel's own timing, not an idealized one.
      const float liveInDb = std::clamp(n->ExtraMeterValue(0), kDynVizMinDb, kDynVizMaxDb);
      const float liveGrDb = std::clamp(n->ExtraMeterValue(1), 0.0f, 40.0f);
      const bool hasSignal = liveInDb > kDynVizMinDb + 0.5f;
      if (hasSignal)
      {
         const float liveOutDb = liveInDb - liveGrDb + makeupDb;
         const float dotX = DynVizDbToX(liveInDb, origin.x, curveW);
         const float unprocessedY = DynVizDbToY(liveInDb, origin.y, h);
         const float dotY = DynVizDbToY(liveOutDb, origin.y, h);

         // Fill the gap between the unprocessed diagonal and the live output
         // - the visible "how much" of the reduction happening right now.
         if (liveGrDb > 0.05f)
         {
            dl->AddLine(ImVec2(dotX, unprocessedY), ImVec2(dotX, dotY),
                        isLight ? IM_COL32(230, 120, 20, 150) : IM_COL32(255, 160, 60, 150), 2.5f);
         }

         const ImU32 dotCol = isLight ? IM_COL32(20, 20, 20, 255) : IM_COL32(255, 255, 255, 255);
         dl->AddCircleFilled(ImVec2(dotX, dotY), 4.0f, dotCol);
         dl->AddCircle(ImVec2(dotX, dotY), 4.0f, ScopeBgCol(), 0, 1.5f);
      }

      dl->PopClipRect();
      dl->AddRect(origin, ImVec2(curveBr.x, br.y), ScopeBorderCol(), 3.0f);

      // Gain-reduction meter, a slim bar riding the curve's right edge -
      // release shows here as the bar's own fall time, since it reads the
      // same smoothed value as the dot rather than the instantaneous one.
      {
         const ImVec2 meterOrigin(curveBr.x + grMeterGap, origin.y);
         const ImVec2 meterBr(br.x, br.y);
         dl->AddRectFilled(meterOrigin, meterBr, ScopeBgCol(), 2.0f);
         const float grT = std::clamp(liveGrDb / 24.0f, 0.0f, 1.0f);
         const float barTop = meterOrigin.y + (meterBr.y - meterOrigin.y) * (1.0f - grT);
         if (grT > 0.005f)
         {
            dl->AddRectFilled(ImVec2(meterOrigin.x + 1.0f, barTop), ImVec2(meterBr.x - 1.0f, meterBr.y),
                              isLight ? IM_COL32(230, 120, 20, 230) : IM_COL32(255, 160, 60, 220), 1.5f);
         }
         dl->AddRect(meterOrigin, meterBr, ScopeBorderCol(), 2.0f);
      }

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         if (hasSignal)
            snprintf(buf, sizeof(buf), "%.1f dB in, -%.1f dB GR, %.0f:1", liveInDb, liveGrDb, ratio);
         else
            snprintf(buf, sizeof(buf), "%.0f dB in -> %.0f dB out, %.0f:1", threshold, threshold + makeupDb, ratio);
         SetAudioReadout("dynamics", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawDynamicsBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "compress %.0f:1 @ %.0f dB", n->Param("ratio"), n->Param("threshold"));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawDynamicsVisualizer(n, n->Param("threshold"), n->Param("ratio"), n->Param("makeup"));
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // KHS Audio Compressor's control surface: threshold, ratio, attack,
      // release, makeup, mix - two rows of three so every knob (mix
      // included) reads as one of the same row instead of mix living apart
      // in its own slider section.
      {
         AudioKnobRow row(3);
         row.Knob("threshold", n->ParamPtr("threshold"), -60.0f, 0.0f, "%.1f dB", kKnobLarge);
         row.Knob("ratio", n->ParamPtr("ratio"), 1.0f, 20.0f, "%.1f:1", kKnobLarge);
         row.Knob("attack", n->ParamPtr("attack"), 0.05f, 200.0f, "%.2f ms", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("release", n->ParamPtr("release"), 5.0f, 2000.0f, "%.0f ms", kKnobLarge);
         row.Knob("makeup", n->ParamPtr("makeup"), -12.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      bool rms = n->Param("detectorRms") != 0.0f;
      if (ModCheckbox("RMS detect##dynRms", &rms))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("detectorRms") = rms ? 1.0f : 0.0f;
      }
      ImGui::SameLine();
      bool external = n->Param("sidechainExternal") != 0.0f;
      if (ModCheckbox("sidechain##dynSidechain", &external))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("sidechainExternal") = external ? 1.0f : 0.0f;
      }

      DrawAnalogToggle(n, "analog##dynAnalog");

      // Turbo 0.60 (upstream): the compressor's style (clean / bus / opto /
      // FET) and a detector-only 90 Hz high-pass, so bass stops pumping.
      {
         const int style = std::clamp((int)(n->Param("style") + 0.5f), 0, DynamicsDsp::kNumStyles - 1);
         DropdownButton("style##dynStyle", DynamicsDsp::StyleList(), style, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("style") = (float)i;
         });
         ImGui::SameLine();
         bool scHpf = n->Param("scHpf") != 0.0f;
         if (ModCheckbox("sc hpf##dynScHpf", &scHpf))
         {
            PushUndoCheckpoint();
            *n->ParamPtr("scHpf") = scHpf ? 1.0f : 0.0f;
         }
      }

      EndAudioBody();
   }

   // ---- Limiter ----------------------------------------------------------
   // A vertical gain-reduction meter, 0/-10/-20 dB scale, bar growing
   // downward from the 0 dB line by the live reduction amount - the shape
   // the reference brief asked for, and distinct enough from
   // DrawStripMeter's plain level bar (no scale, grows up) to warrant its
   // own small ImDrawList routine rather than reusing that primitive.
   void DrawLimiterVisualizer(AudioEffectNode* n, float x, float y, float w, float h)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin(x, y);
      const ImVec2 br(x + w, y + h);
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 3.0f);
      dl->PushClipRect(origin, br, true);

      const float grDb = std::clamp(n->ExtraMeterValue(0), 0.0f, 20.0f);

      static const float kTicks[] = { 0.0f, -10.0f, -20.0f };
      for (float db : kTicks)
      {
         const float t = std::clamp(-db / 20.0f, 0.0f, 1.0f);
         const float ty = y + t * h;
         dl->AddLine(ImVec2(x, ty), ImVec2(br.x, ty), db == 0.0f ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         char buf[8];
         snprintf(buf, sizeof(buf), "%.0f", db);
         dl->AddText(ImVec2(x + 3.0f, ty - (db == 0.0f ? 12.0f : 1.0f)), ScopeTextCol(), buf);
      }

      const float barBottom = y + std::clamp(grDb / 20.0f, 0.0f, 1.0f) * h;
      if (barBottom > y + 1.0f)
         dl->AddRectFilled(ImVec2(x + 4.0f, y), ImVec2(br.x - 4.0f, barBottom),
                           isLight ? IM_COL32(230, 120, 20, 230) : IM_COL32(255, 160, 60, 220), 2.0f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "-%.1f dB GR, out %.1f dB", grDb, n->ExtraMeterValue(1));
         SetAudioReadout("limiter", buf);
      }
   }

   void DrawLimiterBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%.0f dB thresh, -%.1f dB GR", n->Param("threshold"),
               std::clamp(n->ExtraMeterValue(0), 0.0f, 20.0f));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      const float meterW = 64.0f;
      const float meterGap = 12.0f;
      const float meterRightPad = 12.0f;
      const float savedContentW = gAudioContentW;
      const float knobAreaW = savedContentW - meterW - meterGap - meterRightPad;
      const float bodyTop = ImGui::GetCursorScreenPos().y;

      // Two rows of two beside a full-height meter, per the reference
      // layout: threshold/release above, in/out gain below.
      gAudioContentW = knobAreaW;
      {
         AudioKnobRow row(2, kKnobLarge);
         row.Knob("threshold", n->ParamPtr("threshold"), -30.0f, 0.0f, "%.1f dB", kKnobLarge);
         row.Knob("release", n->ParamPtr("release"), 5.0f, 1000.0f, "%.0f ms", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(2, kKnobLarge);
         row.Knob("in gain", n->ParamPtr("inGain"), -12.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("out gain", n->ParamPtr("outGain"), -24.0f, 12.0f, "%.1f dB", kKnobLarge);
         row.End();
      }
      gAudioContentW = savedContentW;

      const float bodyBottom = ImGui::GetCursorScreenPos().y;
      DrawLimiterVisualizer(n, gAudioContentX + knobAreaW + meterGap, bodyTop, meterW, bodyBottom - bodyTop);

      EndAudioBody();
   }

   // ---- Delay ----------------------------------------------------------
   // Decaying tap bars on a time axis, synthesized from feedback (amplitude
   // = feedback^n at n * delay time) - there's only ever one discrete delay
   // path now, so this is exact, not illustrative. Deliberately not a full
   // IR view (README §1's drawing-cost warning): main-thread from params
   // only, except the two meter values for the level pair beneath.
   void DrawDelayVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const float barsH = h - 30.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      // Amplitude gridlines, same 25%-step reading aid Dynamics/Reverb use.
      for (int i = 1; i < 4; i++)
      {
         const float y = origin.y + barsH - (float)i * 0.25f * barsH;
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), ScopeGridCol(), 1.0f);
      }

      const bool sync = n->Param("sync") != 0.0f;
      const int rateDiv = std::clamp((int)(n->Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1);
      float baseSeconds;
      if (sync)
      {
         const double bpm = std::max(1.0f, Transport::Instance().Tempo());
         baseSeconds = (float)(MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv) * 60.0 / bpm);
      }
      else
         baseSeconds = n->Param("timeMs") * 0.001f;

      const float barHalfW = 2.5f;
      const float feedback = std::min(1.0f, n->Param("feedback") / 100.0f); // visualize decay only
      const float windowSeconds = std::max(0.006f, baseSeconds * 6.0f);
      float amp = 1.0f;
      float t = baseSeconds;
      for (int tapIdx = 0; tapIdx < 24 && amp > 0.02f && t < windowSeconds; tapIdx++)
      {
         const float x = origin.x + std::clamp(t / windowSeconds, 0.0f, 1.0f) * w;
         const float barTopY = origin.y + barsH - amp * barsH;
         dl->AddRectFilled(ImVec2(x - barHalfW, barTopY), ImVec2(x + barHalfW, origin.y + barsH),
                           isLight ? IM_COL32(30, 110, 230, 230) : IM_COL32(150, 214, 255, 220));
         amp *= feedback;
         t += baseSeconds;
      }

      dl->AddLine(ImVec2(origin.x, origin.y + barsH), ImVec2(br.x, origin.y + barsH), ScopeMidLineCol(),
                  1.0f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.0f ms  fb %.0f%%", baseSeconds * 1000.0f, feedback * 100.0f);
         SetAudioReadout("delay", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawDelayBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const bool bounce = n->Param("bounce") != 0.0f;

      char stat[96];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %.0f%% fb - %.0f%% wet",
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->Param("feedback"),
                  n->mix * 100.0f);
      else
         snprintf(stat, sizeof(stat), "%.0f ms - %.0f%% fb - %.0f%% wet", n->Param("timeMs"), n->Param("feedback"),
                  n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawDelayVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // KHS Audio Delay's control surface: Time, Tone, Feedback, Pan, Duck,
      // Mix - two rows of three (Bounce is the checkbox below, matching the
      // reference's inline on/off switch). A single row of 6 spread the
      // knobs across the full 440px body with wide, sparse gaps and left the
      // node much wider than it was tall; matching Dynamics' two-row grammar
      // fixes both at once. Rate is a dropdown when synced, a knob when
      // free.
      {
         AudioKnobRow row(3);
         if (sync)
         {
            row.Dropdown("rate", MusicTime::RateDivisionList(), (int)(n->Param("rateDiv") + 0.5f), [n](int i) {
               PushUndoCheckpoint();
               *n->ParamPtr("rateDiv") = (float)i;
            });
         }
         else
         {
            row.Knob("time", n->ParamPtr("timeMs"), 1.0f, 2000.0f, "%.0f ms", kKnobLarge);
         }
         row.Knob("tone", n->ParamPtr("tone"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("feedback", n->ParamPtr("feedback"), 0.0f, 110.0f, "%.0f%%", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("pan", n->ParamPtr("pan"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("duck", n->ParamPtr("ducking"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      bool syncBool = sync;
      if (ModCheckbox("sync to tempo##delaySync", &syncBool))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("sync") = syncBool ? 1.0f : 0.0f;
      }
      ImGui::SameLine();
      bool bounceBool = bounce;
      if (ModCheckbox("bounce##delayBounce", &bounceBool))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("bounce") = bounceBool ? 1.0f : 0.0f;
      }

      DrawAnalogToggle(n, "analog##delayAnalog");

      // Turbo 0.60 (upstream): the tape character, live with analog on.
      // After every older control, so saved modulation ordinals keep their target.
      {
         ImGui::BeginDisabled(n->Param("analog") == 0.0f && !gParamRegisterOnly);
         AudioKnobRow row(4);
         row.Knob("wow", n->ParamPtr("wow"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("flutter", n->ParamPtr("flutter"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("saturation", n->ParamPtr("saturation"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("diffusion", n->ParamPtr("diffusion"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
         ImGui::EndDisabled();
      }

      EndAudioBody();
   }

   // ---- Reverb -----------------------------------------------------------
   // RT60 decay envelope on a time axis, with the predelay gap drawn before
   // the tail begins - a straight line from full amplitude at t=predelay
   // down to -60dB at t=predelay+decay, main-thread from params only (the
   // FDN's actual impulse response is not something worth computing here -
   // README §1's drawing-cost warning - the envelope shape is what a user
   // tweaking size/decay/damping actually needs to see).
   void DrawReverbVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float predelaySeconds = n->Param("predelay") * 0.001f;
      const float decaySeconds = std::max(0.05f, n->Param("decay"));
      const float dampFrac = std::clamp(n->Param("damping"), 0.0f, 1.0f);
      const float windowSeconds = 5.0f;

      for (int sec = 1; sec < (int)windowSeconds; sec++)
      {
         const float x = origin.x + ((float)sec / windowSeconds) * w;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }
      for (int i = 1; i < 4; i++)
      {
         const float y = br.y - (float)i * 0.25f * (h - 6.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), ScopeGridCol(), 1.0f);
      }

      const float predelayX = origin.x + std::clamp(predelaySeconds / windowSeconds, 0.0f, 1.0f) * w;
      dl->AddLine(ImVec2(predelayX, origin.y), ImVec2(predelayX, br.y), ScopeMidLineCol(), 1.0f);

      dl->PathClear();
      const int kNumPoints = 64;
      for (int i = 0; i < kNumPoints; i++)
      {
         const float t = (float)i / (float)(kNumPoints - 1) * windowSeconds;
         float amp;
         if (t < predelaySeconds)
            amp = 1.0f;
         else
         {
            const float tailT = t - predelaySeconds;
            const float rt60Frac = std::clamp(tailT / decaySeconds, 0.0f, 4.0f);
            const float shapedFrac = rt60Frac * (1.0f + dampFrac * 0.5f);
            amp = std::pow(10.0f, -3.0f * shapedFrac);
         }
         const float x = origin.x + (t / windowSeconds) * w;
         const float y = br.y - amp * (h - 6.0f);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.1f s RT60", decaySeconds);
         SetAudioReadout("reverb", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawReverbBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "%.1f s - %.0f%% wet", n->Param("decay"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawReverbVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Tier 1: size, decay, damping, predelay, width, mix - one mode
      // (algorithmic FDN, no engine dropdown). Two rows of 3, matching
      // Dynamics and Delay's grammar. Mix is always the last cell of the
      // last row across every AudioEffects node - the standard bottom-right
      // spot, so it reads the same in every effect regardless of how many
      // params sit ahead of it.
      {
         AudioKnobRow row(3);
         row.Knob("size", n->ParamPtr("size"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("decay", n->ParamPtr("decay"), 0.1f, 20.0f, "%.2f s", kKnobLarge);
         row.Knob("damping", n->ParamPtr("damping"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("predelay", n->ParamPtr("predelay"), 0.0f, 500.0f, "%.0f ms", kKnobLarge);
         row.Knob("width", n->ParamPtr("width"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         bool analogBool = n->Param("analog") != 0.0f;
         if (AudioToggleButton("analog##reverbAnalog", &analogBool, 64.0f))
            *n->ParamPtr("analog") = analogBool ? 1.0f : 0.0f;
      }

      EndAudioBody();
   }

   // ---- Drive ------------------------------------------------------------
   // The transfer curve itself, input -1..+1 -> output -1..+1 - the node's
   // identity, and (per §1.5's spec) main-thread from drive/bias alone, not
   // the live running kernel - same discipline as Dynamics' curve. Tone is
   // a post-shape filter, not part of the static curve, so it's left out
   // here exactly as Delay's tone tilt is left out of the tap-bar view.
   void DrawDriveVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      // Center gridlines (x=0, y=0) plus quarter-amplitude ticks, the same
      // reading aid Dynamics/Reverb give their curves.
      for (int i = -2; i <= 2; i++)
      {
         const float x = origin.x + (0.5f + 0.25f * (float)i) * w;
         const float y = origin.y + (0.5f - 0.25f * (float)i) * h;
         const bool center = (i == 0);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
      }

      // 1:1 reference diagonal (unshaped signal), dim.
      dl->AddLine(ImVec2(origin.x, br.y), ImVec2(br.x, origin.y), ScopeMidLineCol(), 1.0f);

      const float driveDb = n->Param("drive");
      const float bias = n->Param("bias");
      const float color = n->Param("color");

      dl->PathClear();
      const int kNumPoints = 96;
      for (int i = 0; i < kNumPoints; i++)
      {
         const float xIn = -1.0f + 2.0f * (float)i / (float)(kNumPoints - 1);
         const float yOut = std::clamp(DriveDsp::Shape(xIn, driveDb, bias, color), -1.0f, 1.0f);
         const float x = origin.x + (0.5f + 0.5f * xIn) * w;
         const float y = origin.y + (0.5f - 0.5f * yOut) * h;
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "%.1f dB drive, bias %.2f", driveDb, bias);
         SetAudioReadout("drive", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawDriveBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "%.1f dB drive - %.0f%% wet", n->Param("drive"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawDriveVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Tier 1 only: drive, bias, tone, color, output, mix - one processing
      // mode (tanh blended toward arctan via `color`), no curve dropdown.
      // Two rows of three so mix lands in its standard bottom-right spot,
      // matching Dynamics/Delay/Reverb's grammar.
      {
         AudioKnobRow row(3);
         row.Knob("drive", n->ParamPtr("drive"), 0.0f, 40.0f, "%.1f dB", kKnobLarge);
         row.Knob("bias", n->ParamPtr("bias"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("tone", n->ParamPtr("tone"), -1.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("color", n->ParamPtr("color"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("output", n->ParamPtr("output"), -24.0f, 12.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }

   // ---- Wavetable Shaper ---------------------------------------------------
   // The transfer curve itself, input -1..+1 -> output -1..+1, exactly like
   // Drive's - main-thread from params alone via WavetableShaperDsp::Shape,
   // never the live kernel, so it's always meaningful with no audio running.
   // The one addition Drive's curve doesn't have: a live operating-point dot
   // at the kernel's last input sample (ExtraMeterValue(0)), since this curve
   // is otherwise entirely static and the dot is what makes it read as an
   // instrument rather than a diagram.
   void DrawWavetableShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      for (int i = -2; i <= 2; i++)
      {
         const float x = origin.x + (0.5f + 0.25f * (float)i) * w;
         const float y = origin.y + (0.5f - 0.25f * (float)i) * h;
         const bool center = (i == 0);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(br.x, y), center ? ScopeMidLineCol() : ScopeGridCol(), 1.0f);
      }

      // 1:1 reference diagonal (unshaped signal), dim.
      dl->AddLine(ImVec2(origin.x, br.y), ImVec2(br.x, origin.y), ScopeMidLineCol(), 1.0f);

      const int table = (int)(n->Param("table") + 0.5f);
      const float position = n->Param("position");
      const float driveDb = n->Param("drive");
      const float bias = n->Param("bias");
      const float smooth = n->Param("smooth");

      dl->PathClear();
      const int kNumPoints = 96;
      for (int i = 0; i < kNumPoints; i++)
      {
         const float xIn = -1.0f + 2.0f * (float)i / (float)(kNumPoints - 1);
         const float yOut =
            std::clamp(WavetableShaperDsp::Shape(xIn, table, position, driveDb, bias, smooth), -1.0f, 1.0f);
         const float x = origin.x + (0.5f + 0.5f * xIn) * w;
         const float y = origin.y + (0.5f - 0.5f * yOut) * h;
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      // Live operating-point dot at the kernel's last input sample.
      const float lastIn = std::clamp(n->ExtraMeterValue(0), -1.0f, 1.0f);
      const float lastOut =
         std::clamp(WavetableShaperDsp::Shape(lastIn, table, position, driveDb, bias, smooth), -1.0f, 1.0f);
      const ImVec2 dot(origin.x + (0.5f + 0.5f * lastIn) * w, origin.y + (0.5f - 0.5f * lastOut) * h);
      dl->AddCircleFilled(dot, 3.5f, isLight ? IM_COL32(230, 120, 20, 230) : IM_COL32(255, 214, 120, 230));

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "%s, position %.2f, %.1f dB drive", Wavetable::TableName(table), position,
                   driveDb);
         SetAudioReadout("wavetable shaper", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawWavetableShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[96];
      snprintf(stat, sizeof(stat), "%s - %.1f dB drive - %.0f%% wet",
               Wavetable::TableName((int)(n->Param("table") + 0.5f)), n->Param("drive"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawWavetableShaperVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Tier 1 only: table, position, drive, bias, smooth, output, mix - one
      // processing mode. `table` is the curve's identity (a dropdown, same
      // carve-out as Audio Filter's `type`/Ring Mod's `waveform`), not a
      // second mode selector. Row 1 is table/position/drive; row 2 is
      // bias/smooth/output/mix so mix still lands in the standard
      // bottom-right spot every AudioEffects node's grammar puts it in.
      {
         AudioKnobRow row(3);
         const int table = (int)(n->Param("table") + 0.5f);
         row.Dropdown("table", WavetableNames(), table, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("table") = (float)i;
         }, WavetableCategories());
         row.Knob("position", n->ParamPtr("position"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("drive", n->ParamPtr("drive"), 0.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("bias", n->ParamPtr("bias"), -1.0f, 1.0f, "%.2f", kKnobSmall);
         row.Knob("smooth", n->ParamPtr("smooth"), 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.Knob("output", n->ParamPtr("output"), -24.0f, 12.0f, "%.1f dB", kKnobSmall);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.End();
      }

      {
         AudioKnobRow row(1);
         row.Knob("stereo", n->ParamPtr("stereo"), 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.End();
      }

      EndAudioBody();
   }

   // ---- Stereo -------------------------------------------------------------
   // A polar stereo-field imager matching the classic hardware-plugin
   // goniometer layout: concentric dB-ring arcs fanning up from a bottom-
   // center apex, a dB scale along the baseline (loud at the outer edges,
   // quiet toward center), and a filled wedge whose two edges swing left/
   // right of vertical - `width` opens the wedge, `pan` shifts it - the
   // same picture the reference plugin's stereo field display uses.
   // `bassMono` is a frequency-domain effect a polar field can't depict, so
   // it's shown as a text label instead.
   void DrawStereoVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 apex(origin.x + w * 0.5f, br.y - 20.0f);
      const float radius = std::min(w * 0.48f, h - 26.0f);

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      // Concentric dB rings, apex-centered, sweeping only the upper half.
      const int kNumRings = 5;
      for (int i = 1; i <= kNumRings; i++)
      {
         const float r = radius * (float)i / (float)kNumRings;
         dl->PathClear();
         dl->PathArcTo(apex, r, (float)M_PI, 2.0f * (float)M_PI, 32);
         dl->PathStroke(ScopeGridCol(), 0, 1.0f);
      }
      dl->AddLine(ImVec2(apex.x, apex.y), ImVec2(apex.x, apex.y - radius), ScopeMidLineCol(), 1.0f);
      dl->AddLine(ImVec2(apex.x - radius, apex.y), ImVec2(apex.x + radius, apex.y), ScopeMidLineCol(), 1.0f);

      static const char* kDbLabels[] = { "0", "-6", "-12", "-18", "-24" };
      for (int i = 0; i < kNumRings; i++)
      {
         const float t = (float)i / (float)kNumRings;
         const float x = apex.x - radius + t * radius;
         dl->AddText(ImVec2(x - 6.0f, apex.y + 4.0f), ScopeTextCol(), kDbLabels[kNumRings - 1 - i]);
         const float xr = apex.x + radius - t * radius;
         dl->AddText(ImVec2(xr - 6.0f, apex.y + 4.0f), ScopeTextCol(), kDbLabels[kNumRings - 1 - i]);
      }

      const float width = std::clamp(n->Param("width"), 0.0f, 2.0f);
      const float pan = std::clamp(n->Param("pan"), -1.0f, 1.0f);
      const float bassMonoHz = std::max(0.0f, n->Param("bassMono"));

      const float widthNorm = width * 0.5f;
      const float maxHalfAngle = 75.0f * (float)M_PI / 180.0f;
      const float panShift = pan * 30.0f * (float)M_PI / 180.0f;
      const float leftAngle = panShift - maxHalfAngle * widthNorm;
      const float rightAngle = panShift + maxHalfAngle * widthNorm;
      const float wedgeR = radius * 0.92f;

      auto polar = [&](float angle) {
         return ImVec2(apex.x + wedgeR * sinf(angle), apex.y - wedgeR * cosf(angle));
      };
      const ImVec2 leftPt = polar(leftAngle);
      const ImVec2 rightPt = polar(rightAngle);

      dl->PathClear();
      dl->PathLineTo(apex);
      dl->PathLineTo(leftPt);
      dl->PathLineTo(rightPt);
      dl->PathFillConvex(isLight ? IM_COL32(110, 90, 210, 80) : IM_COL32(150, 130, 230, 90));

      dl->PathClear();
      dl->PathLineTo(leftPt);
      dl->PathLineTo(apex);
      dl->PathLineTo(rightPt);
      dl->PathStroke(isLight ? IM_COL32(120, 80, 230, 235) : IM_COL32(190, 170, 255, 235), 0, 1.8f);

      if (bassMonoHz > 0.0f)
      {
         char blo[24];
         snprintf(blo, sizeof(blo), "mono <%.0f Hz", bassMonoHz);
         dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f),
                     isLight ? IM_COL32(210, 100, 20, 230) : IM_COL32(255, 196, 120, 210), blo);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[48];
         const float corr = std::clamp(n->ExtraMeterValue(0), -1.0f, 1.0f);
         snprintf(buf, sizeof(buf), "width %.2f - pan %+.2f - corr %.2f", width, pan, corr);
         SetAudioReadout("stereo", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawStereoBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "width %.2f - pan %.2f", n->Param("width"), n->Param("pan"));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawStereoVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      row.Knob("width", n->ParamPtr("width"), 0.0f, 2.0f, "%.2f", kKnobLarge);
      row.Knob("pan", n->ParamPtr("pan"), -1.0f, 1.0f, "%.2f", kKnobLarge);
      row.Knob("bass mono", n->ParamPtr("bassMono"), 0.0f, 500.0f, "%.0f Hz", kKnobLarge);
      row.End();

      EndAudioBody();
   }

   // ---- Pitch Shifter --------------------------------------------------
   // A simple bipolar meter (-24..+24 semitones) rather than a spectrogram -
   // the shift amount is the node's whole identity and a spectrogram would
   // cost far more to draw than this effect's DSP itself (README §1).
   void DrawPitchShiftVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeGridCol(), 1.0f);
      const float centerX = origin.x + 0.5f * w;
      dl->AddLine(ImVec2(centerX, origin.y), ImVec2(centerX, br.y), ScopeMidLineCol(), 1.0f);

      const float pitch = std::clamp(n->Param("pitch"), -24.0f, 24.0f);
      const float t = 0.5f + 0.5f * (pitch / 24.0f);
      const float markX = origin.x + t * w;
      dl->AddCircleFilled(ImVec2(markX, midY), 6.0f, isLight ? IM_COL32(30, 110, 230, 245) : IM_COL32(150, 214, 255, 245));

      char buf[32];
      snprintf(buf, sizeof(buf), "%+.1f st", pitch);
      dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f), ScopeTextCol(), buf);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
         SetAudioReadout("pitch shifter", buf);

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawPitchShiftBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "%+.1f st - %.0f ms grain", n->Param("pitch"), n->Param("grain"));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawPitchShiftVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      row.Knob("pitch", n->ParamPtr("pitch"), -24.0f, 24.0f, "%+.1f st", kKnobLarge);
      row.Knob("grain", n->ParamPtr("grain"), 10.0f, 250.0f, "%.0f ms", kKnobLarge);
      row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();

      DrawAnalogToggle(n, "analog##psAnalog");
      // Turbo 0.60 (upstream): phase-vocoder mode - keeps chords and mixes
      // intact, at ~85 ms of latency (compensated). Grain only shapes the default mode.
      ImGui::SameLine();
      bool spectral = n->Param("spectral") != 0.0f;
      if (ModCheckbox("spectral##pitchShiftSpectral", &spectral))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("spectral") = spectral ? 1.0f : 0.0f;
      }

      EndAudioBody();
   }

   // Shared rate control for Chorus/Flanger/Phaser - a rateDiv dropdown when
   // synced, a free-Hz knob when not - added as the next cell of an
   // already-open AudioKnobRow so it sits alongside the effect's other
   // knobs instead of claiming a row of its own.
   void AddSyncedRateCell(AudioKnobRow& row, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      if (sync)
      {
         row.Dropdown("rate", MusicTime::RateDivisionList(), (int)(n->Param("rateDiv") + 0.5f), [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("rateDiv") = (float)i;
         });
      }
      else
      {
         row.Knob("rate", n->ParamPtr("rate"), 0.02f, 5.0f, "%.2f Hz", kKnobLarge);
      }
   }

   // "sync to tempo" toggle, drawn below the knob rows.
   void DrawSyncToggle(AudioEffectNode* n, const char* checkboxId)
   {
      bool sync = n->Param("sync") != 0.0f;
      if (ModCheckbox(checkboxId, &sync))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("sync") = sync ? 1.0f : 0.0f;
      }
   }

   // ---- Chorus -------------------------------------------------------------
   // `taps` sine traces (one per voice), phase-offset by `spread` and
   // amplitude-scaled by `depth` - the same "layered waves" picture the
   // BLEASS/Ableton chorus references use, minus the color-per-voice (this
   // codebase's accent color is used throughout, differentiated by alpha
   // instead). `delay`/`feedback` show in the corner label/opacity; `rate`
   // and `mix` are deliberately not reflected here (an animated live LFO
   // costs more to draw than the effect it illustrates - README §1 - and
   // mix never changes a wet effect's own shape).
   void DrawChorusVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      const int taps = n->Param("taps") >= 2.5f ? 3 : 2;
      const float spread = std::clamp(n->Param("spread"), 0.0f, 1.0f);
      const float depthMs = n->Param("depth");
      const float delayMs = n->Param("delay");
      const float feedback = std::clamp(n->Param("feedback"), 0.0f, 0.9f);
      const float amp = std::clamp(depthMs / 15.0f, 0.08f, 1.0f) * (h * 0.5f - 8.0f);

      for (int k = 0; k < taps; k++)
      {
         const float voiceOffset = (float)k / (float)taps;
         const float phaseOffset = voiceOffset + spread * 0.5f * ((float)k / (float)std::max(1, taps - 1));
         const int alpha = std::clamp(150 - k * 40 + (int)(feedback * 90.0f), 60, 255);
         dl->PathClear();
         const int kNumPoints = 96;
         for (int i = 0; i < kNumPoints; i++)
         {
            const float t = (float)i / (float)(kNumPoints - 1);
            const float x = origin.x + t * w;
            const float y = midY - amp * sinf(2.0f * (float)M_PI * (t * 2.0f + phaseOffset));
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, alpha) : IM_COL32(150, 214, 255, alpha), 0, 1.5f + feedback * 1.5f);
      }

      char buf[24];
      snprintf(buf, sizeof(buf), "%.1f ms", delayMs);
      dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f), ScopeTextCol(), buf);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char hover[80];
         snprintf(hover, sizeof(hover), "%.1f ms +/- %.1f ms, %d taps, %.0f%% fb", delayMs, depthMs, taps,
                  feedback * 100.0f);
         SetAudioReadout("chorus", hover);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawChorusBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      char stat[96];
      if (sync)
         snprintf(stat, sizeof(stat), "%.0f taps - %s - %.0f%% wet", n->Param("taps"),
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->mix * 100.0f);
      else
         snprintf(stat, sizeof(stat), "%.0f taps - %.2f Hz - %.0f%% wet", n->Param("taps"), n->Param("rate"),
                  n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawChorusVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("delay", n->ParamPtr("delay"), 1.0f, 30.0f, "%.1f ms", kKnobLarge);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 15.0f, "%.1f ms", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("feedback", n->ParamPtr("feedback"), 0.0f, 0.9f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         AddSyncedRateCell(row, n);
         row.End();
      }

      DrawSyncToggle(n, "sync to tempo##chorusSync");
      ImGui::SameLine();
      bool taps3 = n->Param("taps") >= 2.5f;
      if (ModCheckbox("3 taps##chorusTaps", &taps3))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("taps") = taps3 ? 3.0f : 2.0f;
      }

      ImGui::SameLine();
      DrawAnalogToggle(n, "analog##chorusAnalog");

      EndAudioBody();
   }

   // ---- Flanger --------------------------------------------------------
   // A dry reference line plus a modulated wet trace whose thickness and
   // ripple sharpness scale with `feedback` - a flanger's resonant comb
   // narrowing as feedback climbs is exactly the "jet swoosh" character the
   // knob controls, so this is the one picture that reads that back
   // directly (the Ableton flanger reference's own dual-wave look, adapted
   // to this codebase's single accent color via a dim dry trace instead of
   // a second hue).
   void DrawFlangerVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      const float delayMs = n->Param("delay");
      const float depthMs = n->Param("depth");
      const float feedback = n->Param("feedback");
      const float amp = std::clamp(depthMs / 10.0f, 0.08f, 1.0f) * (h * 0.5f - 10.0f);
      const float rippleAmp = std::fabs(feedback) * (h * 0.16f);
      const float rippleFreq = 3.0f + std::fabs(feedback) * 5.0f;

      // Dry reference - a flat carrier ripple with no modulation envelope.
      dl->PathClear();
      for (int i = 0; i < 96; i++)
      {
         const float t = (float)i / 95.0f;
         const float x = origin.x + t * w;
         const float y = midY - (h * 0.1f) * sinf(2.0f * (float)M_PI * t * rippleFreq);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(ScopeMidLineCol(), 0, 1.0f);

      auto drawWet = [&](float phaseOffset, int alpha) {
         dl->PathClear();
         for (int i = 0; i < 96; i++)
         {
            const float t = (float)i / 95.0f;
            const float x = origin.x + t * w;
            const float envelope = amp * sinf(2.0f * (float)M_PI * (t * 2.0f + phaseOffset));
            const float ripple = rippleAmp * sinf(2.0f * (float)M_PI * t * rippleFreq);
            const float y = midY - envelope - ripple;
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, alpha) : IM_COL32(150, 214, 255, alpha), 0, 1.4f + std::fabs(feedback) * 2.2f);
      };
      drawWet(0.0f, 235);
      drawWet(0.25f, 120);

      char buf[24];
      snprintf(buf, sizeof(buf), "%.2f ms", delayMs);
      dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 6.0f), ScopeTextCol(), buf);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char hover[80];
         snprintf(hover, sizeof(hover), "%.2f ms +/- %.2f ms, %+.0f%% fb", delayMs, depthMs, feedback * 100.0f);
         SetAudioReadout("flanger", hover);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawFlangerBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      char stat[96];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %.0f%% fb - %.0f%% wet",
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->Param("feedback") * 100.0f,
                  n->mix * 100.0f);
      else
         snprintf(stat, sizeof(stat), "%.1f ms - %.0f%% fb - %.0f%% wet", n->Param("delay"),
                  n->Param("feedback") * 100.0f, n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFlangerVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("delay", n->ParamPtr("delay"), 0.2f, 15.0f, "%.2f ms", kKnobLarge);
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 10.0f, "%.2f ms", kKnobLarge);
         row.Knob("feedback", n->ParamPtr("feedback"), -0.95f, 0.95f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(2);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         AddSyncedRateCell(row, n);
         row.End();
      }

      DrawSyncToggle(n, "sync to tempo##flangerSync");

      {
         AudioKnobRow row(2);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         bool analog = n->Param("analog") != 0.0f;
         if (row.Checkbox("analog##flangerAnalog", &analog))
         {
            PushUndoCheckpoint();
            *n->ParamPtr("analog") = analog ? 1.0f : 0.0f;
         }
         row.End();
      }

      EndAudioBody();
   }

   // ---- Phaser -----------------------------------------------------------
   // An illustrative magnitude-response curve on a log-frequency axis with
   // `order/2` notches spaced between cutoff*2^-depth and cutoff*2^+depth -
   // the same sweep range PhaserKernel::ProcessBlock itself computes for
   // its allpass coefficients, just static rather than animated (rate is
   // deliberately not reflected - see DrawChorusVisualizer's comment). A
   // second, dimmer trace offset by `spread` shows the right channel's
   // widened sweep.
   void DrawPhaserVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float logMin = log10f(50.0f), logMax = log10f(8000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(std::max(hz, 1.0f)) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };
      for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }

      const float cutoff = n->Param("cutoff");
      const float depth = std::clamp(n->Param("depth"), 0.0f, 1.0f);
      const int order = std::clamp((int)(n->Param("order") + 0.5f), 2, PhaserKernel::kMaxStages);
      const int notches = std::max(1, order / 2);
      const float spread = std::clamp(n->Param("spread"), 0.0f, 1.0f);
      const float fcLo = std::max(20.0f, cutoff * powf(2.0f, -depth));
      const float fcHi = cutoff * powf(2.0f, depth);
      const float notchWidthLog = std::max(0.05f, 0.55f / (float)notches);

      auto drawTrace = [&](float freqScale, int alpha) {
         dl->PathClear();
         const int kNumPoints = 128;
         for (int i = 0; i < kNumPoints; i++)
         {
            const float t = (float)i / (float)(kNumPoints - 1);
            const float logHz = logMin + t * (logMax - logMin);
            float mag = 1.0f;
            for (int k = 0; k < notches; k++)
            {
               const float frac = notches > 1 ? (float)k / (float)(notches - 1) : 0.5f;
               const float centerHz = std::clamp(fcLo * powf(fcHi / fcLo, frac) * freqScale, 20.0f, 19000.0f);
               const float logDist = (logHz - log10f(centerHz)) / notchWidthLog;
               mag *= 1.0f - 0.85f * expf(-logDist * logDist * 4.0f);
            }
            const float x = origin.x + t * w;
            const float y = br.y - 6.0f - mag * (h - 16.0f);
            dl->PathLineTo(ImVec2(x, y));
         }
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, alpha) : IM_COL32(150, 214, 255, alpha), 0, 1.6f);
      };
      drawTrace(1.0f, 235);
      if (spread > 0.01f)
         drawTrace(powf(2.0f, spread * 0.5f), 110);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "%.0f Hz +/- %.1f oct, %d notches", cutoff, depth, notches);
         SetAudioReadout("phaser", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawPhaserBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      char stat[96];
      if (sync)
         snprintf(stat, sizeof(stat), "%.0f-stage - %s - %.0f%% wet", n->Param("order"),
                  MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)), n->mix * 100.0f);
      else
         snprintf(stat, sizeof(stat), "%.0f-stage - %.0f Hz - %.0f%% wet", n->Param("order"), n->Param("cutoff"),
                  n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawPhaserVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("cutoff", n->ParamPtr("cutoff"), 100.0f, 4000.0f, "%.0f Hz", kKnobLarge);
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("order", n->ParamPtr("order"), 2.0f, (float)PhaserKernel::kMaxStages, "%.0f", kKnobLarge);
         row.End();
      }
      {
         // feedback last: draw order is the modulation pin numbering
         AudioKnobRow row(4);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         AddSyncedRateCell(row, n);
         // The synced rate is a dropdown (hashed pin id) instead of a knob
         // (sequential id); keep feedback's pin number the same either way.
         if (n->Param("sync") != 0.0f)
            gParamCounter++;
         row.Knob("feedback", n->ParamPtr("feedback"), -0.9f, 0.9f, "%.2f", kKnobLarge);
         row.End();
      }

      DrawSyncToggle(n, "sync to tempo##phaserSync");

      DrawAnalogToggle(n, "analog##phaserAnalog");

      EndAudioBody();
   }

   // ---- Bitcrush -----------------------------------------------------------
   // A quantized, sample-and-held sine arch with a solid fill down to the
   // baseline, matching the reference bitcrusher's single-hump filled-
   // staircase look (rather than a centered oscillating line) - `bits`
   // sets the step height (levels on the vertical stairs), `rate` sets the
   // step width (how long each hold lasts).
   void DrawBitcrushVisualizer(AudioEffectNode* n, double sampleRate)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float rateHz = std::max(100.0f, n->Param("rate"));
      const float bits = std::max(1.0f, n->Param("bits"));
      const float levels = powf(2.0f, bits) - 1.0f;
      const float holdSamples = std::max(1.0f, (float)sampleRate / rateHz);
      const float cycleSamples = (float)sampleRate; // one second of illustrative arch at 1 Hz
      const float top = origin.y + 8.0f;
      const float baseY = br.y - 4.0f;

      const int kNumSteps = 160;
      ImVec2 pts[kNumSteps];
      float lastHeld = 0.0f;
      for (int i = 0; i < kNumSteps; i++)
      {
         const float sampleIdx = (float)i / (float)(kNumSteps - 1) * cycleSamples;
         const bool tick = fmodf(sampleIdx, holdSamples) < (cycleSamples / (float)kNumSteps);
         float v = fabsf(sinf((float)M_PI * sampleIdx / cycleSamples));
         if (tick || i == 0)
         {
            v = roundf(v * levels) / levels;
            lastHeld = v;
         }
         else
            v = lastHeld;
         const float x = origin.x + (float)i / (float)(kNumSteps - 1) * w;
         const float y = baseY - v * (baseY - top);
         pts[i] = ImVec2(x, y);
      }

      for (int i = 0; i < kNumSteps - 1; i++)
         dl->AddQuadFilled(pts[i], pts[i + 1], ImVec2(pts[i + 1].x, baseY), ImVec2(pts[i].x, baseY),
                            isLight ? IM_COL32(110, 80, 210, 80) : IM_COL32(150, 110, 230, 110));

      dl->PathClear();
      for (int i = 0; i < kNumSteps; i++)
         dl->PathLineTo(pts[i]);
      dl->PathStroke(isLight ? IM_COL32(120, 80, 230, 245) : IM_COL32(190, 160, 255, 245), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      char buf[48];
      snprintf(buf, sizeof(buf), "%.0f Hz - %.0f bit", rateHz, bits);
      if (ImGui::IsMouseHoveringRect(origin, br))
         SetAudioReadout("bitcrush", buf);

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawBitcrushBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "%.0f Hz - %.0f bit - %.0f%% wet", n->Param("rate"), n->Param("bits"),
               n->mix * 100.0f);

      const double sampleRate =
         AudioEngine::Instance().SampleRate() > 0.0 ? AudioEngine::Instance().SampleRate() : 44100.0;

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawBitcrushVisualizer(n, sampleRate);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      // Labelled "downsample" (not "rate") to match the BLEASS-style
      // reference's own naming - this is the same sample-and-hold rate
      // param as before (BitcrushKernel::kRateHz), a cosmetic relabel only.
      row.Knob("downsample", n->ParamPtr("rate"), 200.0f, 44100.0f, "%.0f Hz", kKnobLarge);
      row.Knob("bits", n->ParamPtr("bits"), 1.0f, 16.0f, "%.0f", kKnobLarge);
      row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();

      DrawAnalogToggle(n, "analog##bitcrushAnalog");
      // Turbo 0.60 (upstream): dither before the bit reduction (softer, noisier steps).
      ImGui::SameLine();
      bool dither = n->Param("dither") != 0.0f;
      if (ModCheckbox("dither##bitcrushDither", &dither))
      {
         PushUndoCheckpoint();
         *n->ParamPtr("dither") = dither ? 1.0f : 0.0f;
      }

      EndAudioBody();
   }

   // ---- Transient Shaper -----------------------------------------------
   // A stylized transient spike (fast rise, slow decay) with the attack/
   // sustain knobs shown as gain bars either side, and the kernel's own
   // live transient-amount (ExtraMeterValue(0)) riding on the curve as a
   // dot - the same "picture from params, dot from the kernel" split
   // Dynamics' transfer curve uses.
   // Local dB range for this visualizer only - deliberately not shared with
   // Dynamics' kDynVizMinDb/kDynVizMaxDb (-60..+6), which would clip a
   // +24 dB attack boost off the top of the plot.
   const float kTsVizMinDb = -48.0f;
   const float kTsVizMaxDb = 24.0f;

   float TsVizDbToY(float db, float y0, float h)
   {
      const float t = (db - kTsVizMinDb) / (kTsVizMaxDb - kTsVizMinDb);
      return y0 + h - std::clamp(t, 0.0f, 1.0f) * h;
   }

   void DrawTransientShaperVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float attackDb = n->Param("attack");
      const float sustainDb = n->Param("sustain");

      const float kWindowSec = 0.4f;
      const int kNumPoints = 96;

      auto envAt = [](float t) {
         return t < 0.08f ? (t / 0.08f) : expf(-(t - 0.08f) * 6.0f);
      };

      auto tauAt = [](float tSec) {
         return expf(-tSec / 0.030f);
      };

      float peakDb = -1000.0f;
      float peakX = origin.x, peakY = origin.y;

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
      {
         const float t = (float)i / (float)(kNumPoints - 1);
         const float env = envAt(t);
         const float ampDb = 20.0f * log10f(std::max(env, 1e-4f));
         const float x = origin.x + t * w;
         const float y = TsVizDbToY(ampDb, origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 60) : IM_COL32(150, 214, 255, 60), 0, 1.5f);

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
      {
         const float t = (float)i / (float)(kNumPoints - 1);
         const float tSec = t * kWindowSec;
         const float env = envAt(t);
         const float tau = tauAt(tSec);
         const float gainDb = attackDb * tau + sustainDb * (1.0f - tau);
         const float ampDb = 20.0f * log10f(std::max(env, 1e-4f)) + gainDb;
         const float x = origin.x + t * w;
         const float y = TsVizDbToY(ampDb, origin.y, h);
         dl->PathLineTo(ImVec2(x, y));
         if (ampDb > peakDb)
         {
            peakDb = ampDb;
            peakX = x;
            peakY = y;
         }
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      const float transientAmount = std::clamp(n->ExtraMeterValue(0), 0.0f, 1.0f);
      dl->AddCircleFilled(ImVec2(peakX, peakY), 3.0f + transientAmount * 4.0f,
                          isLight ? IM_COL32(230, 120, 20, 230) : IM_COL32(255, 196, 120, 230));

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[80];
         snprintf(buf, sizeof(buf), "attack %+.1f dB - sustain %+.1f dB - peak %+.1f dB", attackDb, sustainDb, peakDb);
         SetAudioReadout("transient shaper", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawTransientShaperBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[80];
      snprintf(stat, sizeof(stat), "atk %+.1f dB - sus %+.1f dB", n->Param("attack"), n->Param("sustain"));

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawTransientShaperVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("attack", n->ParamPtr("attack"), -24.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("sustain", n->ParamPtr("sustain"), -24.0f, 24.0f, "%.1f dB", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }

   // ---- Stutter --------------------------------------------------------
   // One square per repeat in the cycle ("1/8 -> 8 steps"), click to mute
   // that repeat - a gate over the beat-repeat loop, same idea as Ableton
   // Beat Repeat's per-slice mute grid. Works at any rate/steps setting:
   // the grid always redraws at exactly `steps` squares.
   void DrawStutterGateGrid(AudioEffectNode* n, int steps)
   {
      const float w = gAudioBodyW;
      const float h = 30.0f;
      const float gap = 3.0f;
      const float cellW = (w - gap * (float)(steps - 1)) / (float)steps;
      const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const int mask = (int)(n->Param("gateMask") + 0.5f);

      for (int i = 0; i < steps; i++)
      {
         const ImVec2 p0(rowOrigin.x + (float)i * (cellW + gap), rowOrigin.y);
         const ImVec2 p1(p0.x + cellW, p0.y + h);
         ImGui::PushID(i);
         ImGui::SetCursorScreenPos(p0);
         ImGui::InvisibleButton("step", ImVec2(cellW, h));
         if (ImGui::IsItemClicked())
         {
            PushUndoCheckpoint();
            *n->ParamPtr("gateMask") = (float)(mask ^ (1 << i));
         }
         const bool on = (mask & (1 << i)) != 0;
         const bool hovered = ImGui::IsItemHovered();
         const bool isLight = IsThemeLight();
         const ImU32 col = on ? (isLight ? IM_COL32(30, 110, 230, hovered ? 255 : 220) : IM_COL32(120, 200, 255, hovered ? 255 : 215))
                               : (isLight ? IM_COL32(220, 225, 235, hovered ? 245 : 215) : IM_COL32(56, 60, 74, hovered ? 210 : 160));
         dl->AddRectFilled(p0, p1, col, 3.0f);
         dl->AddRect(p0, p1, ScopeBorderCol(), 3.0f, 0, 1.0f);
         ImGui::PopID();
      }

      ImGui::SetCursorScreenPos(rowOrigin);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawStutterBody(GraphNode& gn, AudioEffectNode* n)
   {
      const bool sync = n->Param("sync") != 0.0f;
      const int steps = std::clamp((int)(n->Param("steps") + 0.5f), 2, StutterKernel::kMaxGateSteps);

      char stat[80];
      if (sync)
         snprintf(stat, sizeof(stat), "%s - %d steps", MusicTime::RateDivisionName((int)(n->Param("rateDiv") + 0.5f)),
                  steps);
      else
         snprintf(stat, sizeof(stat), "%.0f ms - %d steps", n->Param("timeMs"), steps);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      DrawStutterGateGrid(n, steps);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         // "sync" folded into the row itself (was a standalone checkbox
         // line below the knobs) - one compact row instead of a row plus a
         // line, and rate/time already live right next to the toggle that
         // picks which of the two is showing.
         AudioKnobRow row(3);
         row.Dropdown("sync", { "Free", "Synced" }, sync ? 1 : 0, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("sync") = i != 0 ? 1.0f : 0.0f;
         });
         if (sync)
         {
            row.Dropdown("rate", MusicTime::RateDivisionList(), (int)(n->Param("rateDiv") + 0.5f), [n](int i) {
               PushUndoCheckpoint();
               *n->ParamPtr("rateDiv") = (float)i;
            });
         }
         else
         {
            row.Knob("time", n->ParamPtr("timeMs"), 20.0f, 2000.0f, "%.0f ms", kKnobLarge);
         }
         row.Knob("steps", n->ParamPtr("steps"), 2.0f, (float)StutterKernel::kMaxGateSteps, "%.0f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }

   // ---- Ring Mod ---------------------------------------------------------
   // The modulator waveform itself, one static cycle - the ring-modding
   // oscillator's shape is the node's whole identity (its frequency and
   // waveform, not the input signal, define the sidebands).
   void DrawRingModVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float midY = origin.y + h * 0.5f;
      dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);

      const int waveform = std::clamp((int)(n->Param("waveform") + 0.5f), 0, (int)DspMath::kWaveSquare);
      const float amp = h * 0.5f - 6.0f;

      dl->PathClear();
      const int kNumPoints = 128;
      DspMath::PolyBlepOsc previewOsc;
      previewOsc.phaseInc = 2.0 / (double)kNumPoints; // two static cycles across the panel
      for (int i = 0; i < kNumPoints; i++)
      {
         const double phase = (double)i * previewOsc.phaseInc;
         const float v = previewOsc.Generate(waveform, 0.5f, phase);
         const float x = origin.x + (float)i / (float)(kNumPoints - 1) * w;
         const float y = midY - v * amp;
         dl->PathLineTo(ImVec2(x, y));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[32];
         snprintf(buf, sizeof(buf), "%.0f Hz", n->Param("freq"));
         SetAudioReadout("ring mod", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawRingModBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%.0f Hz - %.0f%% wet", n->Param("freq"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawRingModVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("freq", n->ParamPtr("freq"), 1.0f, 5000.0f, "%.0f Hz", kKnobLarge);
         const int waveform = (int)(n->Param("waveform") + 0.5f);
         static const std::vector<std::string> kWaveNames = { "sine", "triangle", "saw", "square" };
         row.Dropdown("wave", kWaveNames, waveform, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("waveform") = (float)i;
         });
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      DrawAnalogToggle(n, "analog##ringmodAnalog");

      EndAudioBody();
   }

   // ---- Frequency Shifter ----------------------------------------------
   // Comb-displacement visualizer: input partials on the upper row, shifted
   // partials on the lower row with directional arrows showing rigid sideways
   // translation (the defining characteristic of linear frequency shifting vs
   // harmonic pitch scaling), plus a large in-panel numeric Hz readout.
   void DrawFrequencyShifterVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float shift = n->Param("shift");
      const float spread = n->Param("spread");
      const float midX = origin.x + w * 0.5f;
      const float topY = origin.y + h * 0.30f;
      const float botY = origin.y + h * 0.72f;

      // Center frequency axis & rows
      dl->AddLine(ImVec2(origin.x + 12.0f, topY), ImVec2(br.x - 12.0f, topY), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(origin.x + 12.0f, botY), ImVec2(br.x - 12.0f, botY), ScopeGridCol(), 1.0f);
      dl->AddLine(ImVec2(midX, origin.y + 6.0f), ImVec2(midX, br.y - 6.0f), ScopeMidLineCol(), 1.0f);

      // Partials comb: 7 evenly spaced input partials
      const int kNumPartials = 7;
      const float kSpacing = w * 0.085f;
      const float kMaxShiftPix = w * 0.28f;
      const int range = std::clamp((int)(n->Param("range") + 0.5f), 0, 1);
      const float maxShiftSpan = (range == 0) ? 100.0f : 10000.0f;
      const float shiftFrac = std::clamp(shift / maxShiftSpan, -1.0f, 1.0f);
      const float dxL = shiftFrac * kMaxShiftPix;

      // Arrow & shifted partial color: amber for up-shift, coral/magenta for down-shift, blue-grey for zero
      const ImU32 arrowCol = (shift > 0.5f)
         ? (isLight ? IM_COL32(210, 120, 20, 240) : IM_COL32(255, 185, 70, 230))
         : ((shift < -0.5f) ? (isLight ? IM_COL32(220, 40, 70, 240) : IM_COL32(255, 110, 130, 230))
                            : (isLight ? IM_COL32(90, 105, 130, 200) : IM_COL32(140, 160, 195, 180)));

      for (int i = 0; i < kNumPartials; i++)
      {
         const float xIn = midX + (float)(i - kNumPartials / 2) * kSpacing;
         const float xOutL = xIn + dxL;

         // Input partial tick (upper row)
         dl->AddLine(ImVec2(xIn, topY - 7.0f), ImVec2(xIn, topY + 3.0f),
                     isLight ? IM_COL32(70, 90, 130, 220) : IM_COL32(150, 170, 205, 210), 1.5f);

         // Shifted partial tick (lower row)
         dl->AddLine(ImVec2(xOutL, botY - 3.0f), ImVec2(xOutL, botY + 7.0f), arrowCol, 1.8f);

         // Connecting arrow from input to shifted partial
         dl->AddLine(ImVec2(xIn, topY + 3.0f), ImVec2(xOutL, botY - 3.0f), arrowCol, 1.2f);
         if (std::fabs(dxL) > 1.5f || std::fabs(topY - botY) > 5.0f)
         {
            const float arrowSz = 3.0f;
            dl->AddTriangleFilled(ImVec2(xOutL, botY - 2.0f),
                                  ImVec2(xOutL - arrowSz, botY - 2.0f - arrowSz * 1.5f),
                                  ImVec2(xOutL + arrowSz, botY - 2.0f - arrowSz * 1.5f),
                                  arrowCol);
         }

         if (spread > 0.5f)
         {
            const float shiftRFrac = std::clamp((shift + spread) / maxShiftSpan, -1.0f, 1.0f);
            const float dxR = shiftRFrac * kMaxShiftPix;
            const float xOutR = xIn + dxR;
            const ImU32 spreadCol = (shift + spread > 0.5f)
               ? (isLight ? IM_COL32(210, 140, 40, 140) : IM_COL32(255, 205, 110, 120))
               : ((shift + spread < -0.5f) ? (isLight ? IM_COL32(210, 80, 100, 140) : IM_COL32(255, 140, 160, 120))
                                           : (isLight ? IM_COL32(90, 105, 130, 120) : IM_COL32(140, 160, 195, 100)));
            dl->AddLine(ImVec2(xOutR, botY - 2.0f), ImVec2(xOutR, botY + 6.0f), spreadCol, 1.2f);
         }
      }

      char valBuf[64];
      auto FmtFreq = [](float f, char* b, size_t sz, bool withSign) {
         if (std::fabs(f) >= 1000.0f)
            snprintf(b, sz, withSign ? "%+.2f kHz" : "%.2f kHz", f / 1000.0f);
         else
            snprintf(b, sz, withSign ? "%+.0f Hz" : "%.0f Hz", f);
      };

      char sBuf[32], rBuf[32];
      FmtFreq(shift, sBuf, sizeof(sBuf), true);
      if (std::fabs(spread) > 0.5f)
      {
         FmtFreq(shift + spread, rBuf, sizeof(rBuf), true);
         snprintf(valBuf, sizeof(valBuf), "%s / %s", sBuf, rBuf);
      }
      else
      {
         snprintf(valBuf, sizeof(valBuf), "%s", sBuf);
      }

      const ImVec2 textSz = ImGui::CalcTextSize(valBuf);
      dl->AddText(ImVec2(br.x - textSz.x - 10.0f, origin.y + 8.0f),
                  isLight ? IM_COL32(40, 55, 80, 240) : IM_COL32(230, 238, 255, 240), valBuf);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         SetAudioReadout("freq shift", valBuf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawFrequencyShifterBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      const float shiftVal = n->Param("shift");
      if (std::fabs(shiftVal) >= 1000.0f)
         snprintf(stat, sizeof(stat), "%+.2f kHz - %.0f%% fb - %.0f%% wet",
                  shiftVal / 1000.0f, n->Param("feedback") * 100.0f, n->mix * 100.0f);
      else
         snprintf(stat, sizeof(stat), "%+.0f Hz - %.0f%% fb - %.0f%% wet",
                  shiftVal, n->Param("feedback") * 100.0f, n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFrequencyShifterVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         const int range = std::clamp((int)(n->Param("range") + 0.5f), 0, 1);
         const float shiftLimit = (range == 0) ? 100.0f : 10000.0f;
         static const std::vector<std::string> kRangeOptions = { "fine +/-100", "wide +/-10k" };
         const char* fmt = (range == 0) ? "%+.1f Hz" : (std::fabs(shiftVal) >= 1000.0f ? "%+.2f kHz" : "%+.0f Hz");

         AudioKnobRow row(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row.DropdownKnob("fsRange", kRangeOptions, range,
                          [n](int i) {
                             PushUndoCheckpoint();
                             *n->ParamPtr("range") = (float)i;
                          },
                          "shift", n->ParamPtr("shift"), -shiftLimit, shiftLimit, fmt, false, kKnobLarge);
         row.Knob("feedback", n->ParamPtr("feedback"), 0.0f, 0.95f, "%.2f", kKnobLarge);
         row.Knob("spread", n->ParamPtr("spread"), 0.0f, 100.0f, "%.1f Hz", kKnobLarge);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      DrawAnalogToggle(n, "analog##freqshiftAnalog");

      EndAudioBody();
   }

   // ---- Tremolo --------------------------------------------------------
   // The resulting *gain envelope*, not the raw bipolar LFO - a filled area
   // from the panel top down to the curve so the eye reads "how much signal
   // survives" directly, exactly what a tremolo pedal's amplitude modulation
   // does and Ring Mod's raw-oscillator preview (above) deliberately does
   // not - see TremoloKernel.h's class comment on the distinction. The right
   // channel's envelope overlays as a second, dimmer stroke whenever
   // `stereoPhase` is non-zero, since that knob is otherwise invisible.
   void DrawTremoloVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const int shape = std::clamp((int)(n->Param("shape") + 0.5f), 0, 3);
      const int dspWaveform = TremoloKernel::ShapeToDspWaveform(shape);
      const bool rampDown = shape == 3;
      const float depth = std::clamp(n->Param("depth"), 0.0f, 1.0f);
      const float stereoPhaseCycles = std::clamp(n->Param("stereoPhase"), 0.0f, 180.0f) / 360.0f;

      DspMath::PolyBlepOsc previewOsc;
      previewOsc.phaseInc = 2.0 / (double)128; // two static cycles across the panel

      auto envelopeY = [&](double phase) {
         float lfo = previewOsc.Generate(dspWaveform, 0.5f, phase);
         if (rampDown)
            lfo = -lfo;
         const float gain = TremoloKernel::GainFromLfo(lfo, depth);
         return br.y - gain * h;
      };

      const int kNumPoints = 128;
      ImVec2 envPts[kNumPoints];
      for (int i = 0; i < kNumPoints; i++)
      {
         const double phase = (double)i * previewOsc.phaseInc;
         const float x = origin.x + (float)i / (float)(kNumPoints - 1) * w;
         envPts[i] = ImVec2(x, envelopeY(phase));
      }
      for (int i = 0; i < kNumPoints - 1; i++)
         dl->AddQuadFilled(envPts[i], envPts[i + 1], ImVec2(envPts[i + 1].x, br.y), ImVec2(envPts[i].x, br.y),
                            isLight ? IM_COL32(30, 110, 230, 45) : IM_COL32(150, 214, 255, 60));

      dl->PathClear();
      for (int i = 0; i < kNumPoints; i++)
         dl->PathLineTo(envPts[i]);
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      if (stereoPhaseCycles > 0.0f)
      {
         dl->PathClear();
         for (int i = 0; i < kNumPoints; i++)
         {
            const double phase = (double)i * previewOsc.phaseInc + stereoPhaseCycles;
            const float x = origin.x + (float)i / (float)(kNumPoints - 1) * w;
            dl->PathLineTo(ImVec2(x, envelopeY(phase)));
         }
         dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 110) : IM_COL32(150, 214, 255, 110), 0, 1.4f);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[32];
         const bool sync = n->Param("sync") != 0.0f;
         if (sync)
         {
            const int rateDiv = (int)(n->Param("rateDiv") + 0.5f);
            const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
            const double rateHz = MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
            snprintf(buf, sizeof(buf), "%s (%.1f Hz)", MusicTime::RateDivisionName(rateDiv), rateHz);
         }
         else
            snprintf(buf, sizeof(buf), "%.1f Hz", n->Param("rate"));
         SetAudioReadout("tremolo", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawTremoloBody(GraphNode& gn, AudioEffectNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%.0f%% depth - %.0f%% wet", n->Param("depth") * 100.0f, n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawTremoloVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Knob("depth", n->ParamPtr("depth"), 0.0f, 1.0f, "%.2f", kKnobLarge);
         const int shape = (int)(n->Param("shape") + 0.5f);
         static const std::vector<std::string> kShapeNames = { "sine", "triangle", "square", "ramp down" };
         row.Dropdown("shape", kShapeNames, shape, [n](int i) {
            PushUndoCheckpoint();
            *n->ParamPtr("shape") = (float)i;
         });
         row.Knob("stereo", n->ParamPtr("stereoPhase"), 0.0f, 180.0f, "%.0f deg", kKnobLarge);
         row.End();
      }
      {
         AudioKnobRow row(3);
         if (n->Param("sync") != 0.0f)
         {
            row.Dropdown("rate", MusicTime::RateDivisionList(), (int)(n->Param("rateDiv") + 0.5f), [n](int i) {
               PushUndoCheckpoint();
               *n->ParamPtr("rateDiv") = (float)i;
            });
         }
         else
         {
            row.Knob("rate", n->ParamPtr("rate"), 0.05f, 20.0f, "%.2f Hz", kKnobLarge);
         }
         row.Skip();
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      DrawSyncToggle(n, "sync to tempo##tremoloSync");

      EndAudioBody();
   }

   // ---- Formant Filter -----------------------------------------------------
   // The three formant resonators' frequencies as markers on a log axis -
   // the picture a filter's own frequency-response curve would give, but
   // for three narrow bandpass peaks rather than one broad shape, so three
   // labelled markers reads more clearly than a busy triple-peaked curve.
   void DrawFormantFilterVisualizer(AudioEffectNode* n)
   {
      const float w = gAudioBodyW;
      const float h = kAudioTimeVizH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const float logMin = log10f(100.0f), logMax = log10f(5000.0f);
      auto freqToX = [&](float hz) {
         const float t = std::clamp((log10f(hz) - logMin) / (logMax - logMin), 0.0f, 1.0f);
         return origin.x + t * w;
      };

      for (float hz : { 100.0f, 300.0f, 1000.0f, 3000.0f })
      {
         const float x = freqToX(hz);
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), ScopeGridCol(), 1.0f);
      }

      const FormantDsp::Formants f = FormantDsp::VowelFormants(n->Param("vowel"));
      const float formants[3] = { f.f1, f.f2, f.f3 };
      const float weights[3] = { 1.0f, 0.7f, 0.5f };
      static const char* kFormantLabels[3] = { "F1", "F2", "F3" };
      for (int i = 0; i < 3; i++)
      {
         const float x = freqToX(formants[i]);
         const float peakY = br.y - 6.0f - weights[i] * (h - 20.0f);
         dl->AddLine(ImVec2(x, br.y - 6.0f), ImVec2(x, peakY),
                     isLight ? IM_COL32(30, 110, 230, 230) : IM_COL32(150, 214, 255, 220), 3.0f);
         dl->AddText(ImVec2(x - 7.0f, peakY - 14.0f), ScopeTextCol(), kFormantLabels[i]);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      if (ImGui::IsMouseHoveringRect(origin, br))
      {
         char buf[64];
         snprintf(buf, sizeof(buf), "F1 %.0f - F2 %.0f - F3 %.0f Hz", f.f1, f.f2, f.f3);
         SetAudioReadout("formant filter", buf);
      }

      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawFormantFilterBody(GraphNode& gn, AudioEffectNode* n)
   {
      static const char* kVowelNames[] = { "A", "E", "I", "O", "U" };
      const int vowelIdx = std::clamp((int)(n->Param("vowel") + 0.5f), 0, 4);

      char stat[64];
      snprintf(stat, sizeof(stat), "%s - Q %.1f - %.0f%% wet", kVowelNames[vowelIdx], n->Param("q"), n->mix * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawFormantFilterVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioKnobRow row(3);
      row.Knob("vowel", n->ParamPtr("vowel"), 0.0f, 4.0f, "%.2f", kKnobLarge);
      row.Knob("Q", n->ParamPtr("q"), 2.0f, 30.0f, "%.1f", kKnobLarge);
      row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
      row.End();

      EndAudioBody();
   }

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
            ModSlider(label, &m.value, m.minValue, m.maxValue, "%.3f", cellW, /*audioStyle=*/true);
            // Right-click an assigned row to unmap it. The row itself stays -
            // it just goes back to showing "unassigned".
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
               PushUndoCheckpoint();
               n->UnmapSlot(i);
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

   // Declared further down (with the rest of the patch-state globals); used
   // here to name a fresh recording after the open patch.
   extern std::string gPatchPath;
   extern bool gPatchDirty;

   // Filename for a fresh recording that doesn't collide with anything
   // already in `dir`: Infinite-<patchname>-001.ext, -002, ... <patchname>
   // falls back to "Untitled" for an unsaved patch, same fallback the title
   // bar uses. `dir` may be "" (only used to probe collisions, e.g. for the
   // Choose... dialog's suggested name), in which case every candidate is
   // reported free.
   std::string DefaultRecordingFileName(const std::string& dir, const std::string& ext = "wav")
   {
      std::string base = gPatchPath.empty() ? "Untitled" : gPatchPath.substr(gPatchPath.find_last_of("/\\") + 1);
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
   // or ~/Desktop when nothing has been picked yet.
   std::string RecordingDirFor(const AudioOutputNode* n)
   {
      if (!n->recordDirectory.empty())
         return n->recordDirectory;
      const std::string desktop = InfiniteDesktopDirectory();
      return desktop.empty() ? "." : desktop;
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

      // Turbo: live monitoring - skip the multi-output delay compensation.
      {
         bool live = n->live;
         if (ImGui::Checkbox("live (no delay compensation)##audioOutLive", &live))
         {
            PushUndoCheckpoint();
            n->live = live;
            RebuildAudioTopology();
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("For a mic -> plugin -> headphones path: this output is never delayed\n"
                              "to line up with slower branches (a lookahead limiter, a pitch shifter,\n"
                              "a plugin with latency) feeding other Audio Outs.");
      }

      // Format selector: WAV | FLAC | MP3. A CV value of 0..1 is divided
      // evenly across the available entries by DropdownButton.
      ImGui::BeginDisabled(recording);
      AudioBareDropdown("audioOutFormat", {"WAV", "FLAC", "MP3"}, n->formatIndex,
                        [n](int i) {
                           if (!n->IsRecording()) { n->formatIndex = i; gPatchDirty = true; }
                        }, AudioFullWidth());
      ImGui::EndDisabled();
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Record/Stop is the only control that ever starts or stops a
      // recording - Choose... below only ever changes where the *next*
      // recording will land, never triggers one itself, so picking a folder
      // can't be mistaken for pressing Record.
      const float btnW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.18f, 1.0f));
      ImGui::BeginDisabled(!recording && !audioOn);
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop##audioOutRec" : "Record##audioOutRec",
                         recording, requestedRecording, ImVec2(btnW, 0)))
      {
         if (!requestedRecording)
         {
            n->StopRecording();
         }
         else if (audioOn)
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
         const std::string initial = RecordingDirFor(n);
         StartNodeFileDialog(n, [initial] { return Platform::OpenFolderDialog("Choose recording folder", initial); },
                             [](auto* m, const std::string& chosen) {
                                m->recordDirectory = chosen;
                                gPatchDirty = true;
                             });
      }
      ImGui::EndDisabled();

      const std::string destLabel = n->recordDirectory.empty() ? std::string("~/Desktop") : n->recordDirectory;
      ImGui::TextColored(ImVec4(0.6f, 0.62f, 0.68f, 1.0f), "%s", destLabel.c_str());

      if (n->DroppedSampleCount() > 0)
         ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1.0f), "dropped samples - device can't keep up");

      EndAudioBody();
   }

   // Dispatch for anything IsAudioBodyNode() accepts. Called from the
   // node-body loop's DrawPreview-replacement chain; audio nodes have no
   // image to preview, so this entirely replaces DrawPreview +
   // DrawXxxParams for them rather than sitting alongside either.
